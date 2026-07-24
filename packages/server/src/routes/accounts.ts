import type { FastifyInstance } from "fastify";
import * as repo from "../db/repository.js";
import { accountManager } from "../ilink/manager.js";
import type { LoginStartResponse, LoginStatusResponse } from "@workbench/shared";

const API_BASE = "https://ilinkai.weixin.qq.com";
const BOT_TYPE = "3";

/** SDK stores account data in ~/.openclaw/openclaw-weixin/accounts/ */
function sdkAccountDir(): string {
  const dir = process.env.OPENCLAW_STATE_DIR || process.env.CLAWDBOT_STATE_DIR || require("node:path").join(require("node:os").homedir(), ".openclaw");
  return require("node:path").join(dir, "openclaw-weixin", "accounts");
}

/** Low-level iLink API calls (SDK doesn't export these) */
async function ilinkGenerateQR(): Promise<{ qrcode: string; qrcodeUrl: string }> {
  const url = `${API_BASE}/ilink/bot/get_bot_qrcode?bot_type=${BOT_TYPE}`;
  const res = await fetch(url, {
    headers: { AuthorizationType: "ilink_bot_token", "X-WECHAT-UIN": "fixed-uin" },
  });
  const data = await res.json() as any;
  return { qrcode: data.qrcode, qrcodeUrl: data.qrcode_img_content };
}

async function ilinkPollQR(qrcode: string, timeoutMs: number): Promise<{
  status: string; accountId?: string; botToken?: string; baseUrl?: string; userId?: string;
}> {
  const url = `${API_BASE}/ilink/bot/get_qrcode_status?qrcode=${encodeURIComponent(qrcode)}`;
  const controller = new AbortController();
  const t = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetch(url, {
      headers: { AuthorizationType: "ilink_bot_token", "X-WECHAT-UIN": "fixed-uin" },
      signal: controller.signal,
    });
    const data = await res.json() as any;
    return {
      status: (data.status as string) ?? "wait",
      accountId: data.ilink_bot_id as string,
      botToken: data.bot_token as string,
      baseUrl: data.baseurl as string,
      userId: data.ilink_user_id as string,
    };
  } catch (e) {
    if (e instanceof Error && e.name === "AbortError") return { status: "wait" };
    throw e;
  } finally { clearTimeout(t); }
}

export async function accountRoutes(app: FastifyInstance) {
  // ---- Login flow ----

  /** Start QR code login for an account placeholder */
  app.post<{ Params: { id: string } }>("/api/accounts/:id/login", async (req, reply) => {
    try {
      const result = await ilinkGenerateQR();
      // Store as pending account
      const now = Date.now();
      await repo.upsertAccount({
        id: req.params.id,
        label: req.params.id,
        botToken: "",
        baseUrl: API_BASE,
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
    accountManager.stopBot(req.params.id);
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
      const status = await ilinkPollQR(qrcode, refreshCount === 0 ? 60_000 : 35_000);

      if (status.status === "scaned") {
        await repo.updateAccountStatus(accountId, "scanning");
      } else if (status.status === "confirmed") {
        if (!status.accountId) throw new Error("No accountId in confirmed response");
        if (!status.botToken) throw new Error("No botToken in confirmed response");

        const token = status.botToken;
        const baseUrl = status.baseUrl ?? API_BASE;
        const userId = status.userId ?? "";
        const realAccountId = status.accountId;

        // Delete placeholder if id differs
        if (realAccountId !== accountId) {
          await repo.deleteAccount(accountId);
        }

        const now = Date.now();
        await repo.upsertAccount({
          id: realAccountId,
          label: realAccountId,
          botToken: token,
          baseUrl,
          status: "online",
          avatar: null,
          nickname: null,
          wechatId: userId,
          createdAt: now,
          updatedAt: now,
        });

        // Add scanner to allowFrom whitelist
        if (userId) {
          await repo.addAllowFrom(realAccountId, userId);
        }

        // Save to SDK's file storage so start() can find it
        // SDK stores files at ~/.openclaw/openclaw-weixin/accounts/{normalizedId}.json
        {
          const fs = require("node:fs") as typeof import("fs");
          const path = require("node:path") as typeof import("path");
          const os = require("node:os") as typeof import("os");
          const normalizedId = realAccountId.trim().toLowerCase().replace(/[@.]/g, "-");
          const accountsDir = path.join(os.homedir(), ".openclaw", "openclaw-weixin", "accounts");
          const indexDir = path.join(os.homedir(), ".openclaw", "openclaw-weixin");
          fs.mkdirSync(accountsDir, { recursive: true });
          fs.writeFileSync(path.join(accountsDir, `${normalizedId}.json`), JSON.stringify({
            token, baseUrl, userId: userId || undefined,
            savedAt: new Date().toISOString(),
          }, null, 2), "utf-8");
          // Append to account index (don't overwrite existing)
          let index: string[] = [];
          try {
            const existing = fs.readFileSync(path.join(indexDir, "accounts.json"), "utf-8");
            index = JSON.parse(existing);
          } catch { /* no existing index */ }
          if (!index.includes(normalizedId)) index.push(normalizedId);
          fs.writeFileSync(path.join(indexDir, "accounts.json"), JSON.stringify(index, null, 2), "utf-8");
        }

        // Start bot via SDK
        accountManager.startBot(realAccountId);
        return;
      } else if (status.status === "expired") {
        refreshCount++;
        if (refreshCount > 3) {
          await repo.updateAccountStatus(accountId, "error");
          return;
        }
        // Refresh QR
        const newQr = await ilinkGenerateQR();
        qrcode = newQr.qrcode;
        console.log(`[Login:${accountId}] QR refreshed (${refreshCount}/3)`);
      }
    } catch (e) {
      console.error(`[Login:${accountId}] Poll error: ${e}`);
      await new Promise((r) => setTimeout(r, 2000));
    }
  }

  await repo.updateAccountStatus(accountId, "error");
}
