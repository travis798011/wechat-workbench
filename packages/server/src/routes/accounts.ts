import type { FastifyInstance } from "fastify";
import * as repo from "../db/repository.js";
import * as ilink from "../ilink/client.js";
import { accountManager } from "../ilink/manager.js";
import type { LoginStartResponse, LoginStatusResponse } from "@workbench/shared";

export async function accountRoutes(app: FastifyInstance) {
  // ---- Login flow ----

  /** Start QR code login for an account placeholder */
  app.post<{ Params: { id: string } }>("/api/accounts/:id/login", async (req, reply) => {
    try {
      const result = await ilink.generateQR();
      // Store as pending account
      const now = Date.now();
      await repo.upsertAccount({
        id: req.params.id,
        label: req.params.id,
        botToken: "",
        baseUrl: ilink.API_BASE,
        status: "qr_pending",
        avatar: null,
        nickname: null,
        wechatId: null,
        createdAt: now,
        updatedAt: now,
      });

      // Start polling in background
      const poller = pollLoginStatus(req.params.id, result.qrcode);
      poller.catch((e) => {
        console.error(`[Login:${req.params.id}] Polling error: ${e}`);
      });

      const resp: LoginStartResponse = {
        qrcode: result.qrcode,
        qrcodeUrl: result.qrcodeUrl,
      };
      return reply.send(resp);
    } catch (e) {
      return reply.status(500).send({ error: String(e) });
    }
  });

  /** Get current login status */
  app.get<{ Params: { id: string } }>("/api/accounts/:id/login/status", async (req, reply) => {
    const acct = await repo.getAccount(req.params.id);
    if (!acct) return reply.status(404).send({ error: "Account not found" });

    const resp: LoginStatusResponse = {
      status: acct.status === "qr_pending" ? "wait"
        : acct.status === "scanning" ? "scaned"
        : acct.status === "online" ? "confirmed"
        : "expired",
    };
    return reply.send(resp);
  });

  // ---- CRUD ----

  app.get("/api/accounts", async (_req, reply) => {
    const accounts = await repo.listAccounts();
    return reply.send(accounts.map((a) => ({
      ...a,
      botToken: undefined, // never expose token
    })));
  });

  app.patch<{ Params: { id: string }; Body: { label?: string } }>("/api/accounts/:id", async (req, reply) => {
    const acct = await repo.getAccount(req.params.id);
    if (!acct) return reply.status(404).send({ error: "Not found" });
    if (req.body.label) {
      acct.label = req.body.label;
      await repo.upsertAccount(acct);
    }
    return reply.send({ ok: true });
  });

  app.delete<{ Params: { id: string } }>("/api/accounts/:id", async (req, reply) => {
    accountManager.stopMonitor(req.params.id);
    await repo.deleteAccount(req.params.id);
    return reply.send({ ok: true });
  });

  /** Get allowFrom whitelist for an account */
  app.get<{ Params: { id: string } }>("/api/accounts/:id/allowFrom", async (req, reply) => {
    const users = await repo.getAllowFrom(req.params.id);
    return reply.send(users);
  });

  /** Add a user to the allowFrom whitelist */
  app.post<{ Params: { id: string }; Body: { userId: string } }>(
    "/api/accounts/:id/allowFrom",
    async (req, reply) => {
      if (!req.body.userId) return reply.status(400).send({ error: "userId required" });
      const ok = await repo.addAllowFrom(req.params.id, req.body.userId);
      return reply.send({ ok });
    },
  );
}

/** Background polling for QR code scan status */
async function pollLoginStatus(accountId: string, qrcode: string): Promise<void> {
  const deadline = Date.now() + 480_000; // 8 minutes
  let refreshCount = 0;

  while (Date.now() < deadline) {
    try {
      const status = await ilink.pollQR(qrcode, refreshCount === 0 ? 60_000 : 35_000);

      if (status.status === "scaned") {
        await repo.updateAccountStatus(accountId, "scanning");
      } else if (status.status === "confirmed") {
        if (!status.accountId) throw new Error("No accountId in confirmed response");

        // Use the real ilink_bot_id as the account id
        const token = status.botToken ?? "";
        const baseUrl = status.baseUrl ?? ilink.API_BASE;
        const userId = status.userId ?? "";

        // Delete placeholder if id differs
        if (status.accountId !== accountId) {
          await repo.deleteAccount(accountId);
        }

        const now = Date.now();
        await repo.upsertAccount({
          id: status.accountId,
          label: status.accountId,
          botToken: token,
          baseUrl,
          status: "online",
          avatar: null,
          nickname: null,
          wechatId: userId, // store scanner's wechat user ID
          createdAt: now,
          updatedAt: now,
        });

        // Add scanner to allowFrom whitelist
        if (userId) {
          await repo.addAllowFrom(status.accountId, userId);
        }

        // Start message monitor
        accountManager.startMonitor(status.accountId, token);
        return; // Done!
      } else if (status.status === "expired") {
        refreshCount++;
        if (refreshCount > 3) {
          await repo.updateAccountStatus(accountId, "error");
          return;
        }
        // Refresh QR
        const newQr = await ilink.generateQR();
        qrcode = newQr.qrcode;
        console.log(`[Login:${accountId}] QR refreshed (${refreshCount}/3)`);
      }
      // "wait" → keep polling
    } catch (e) {
      console.error(`[Login:${accountId}] Poll error: ${e}`);
      await new Promise((r) => setTimeout(r, 2000));
    }
  }

  await repo.updateAccountStatus(accountId, "error");
}
