import type { FastifyInstance } from "fastify";
import * as repo from "../db/repository.js";
import * as ilink from "../ilink/client.js";
import { accountManager } from "../ilink/manager.js";
import { enqueueSend } from "./wcf-bridge.js";
import type { SendMessageRequest, SendMessageResponse, MessagesQuery } from "@workbench/shared";

export async function messageRoutes(app: FastifyInstance) {
  /** Get message history for a contact */
  app.get<{ Querystring: MessagesQuery }>("/api/messages", async (req, reply) => {
    const { contactId, before, limit } = req.query;
    if (!contactId) return reply.status(400).send({ error: "contactId is required" });
    const msgs = await repo.listMessages(contactId, before ? Number(before) : undefined, limit ? Number(limit) : 50);
    return reply.send(msgs);
  });

  /** Send a message */
  app.post<{ Body: SendMessageRequest }>("/api/messages/send", async (req, reply) => {
    const { accountId, toUserId, msgType, content, contextToken } = req.body;

    if (!accountId || !toUserId || !content) {
      return reply.status(400).send({ error: "accountId, toUserId, content required" });
    }

    // Get account
    const acct = await repo.getAccount(accountId);
    if (!acct) return reply.status(404).send({ error: "Account not found" });

    // Determine if this is a wcf account or iLink account
    // wcf accounts have empty botToken, iLink accounts have non-empty botToken
    if (acct.botToken && acct.baseUrl) {
      // iLink route: send directly
      try {
        if (msgType === "text") {
          await ilink.sendMessage(acct.botToken, { toUserId, text: content, contextToken });
        }
      } catch (e) {
        return reply.status(500).send({ error: `iLink send failed: ${String(e)}` });
      }
    } else {
      // wcf route: add to send queue for bridge to pick up
      enqueueSend(accountId, toUserId, content);
    }

    // Store outbound message
    const contactId = `${accountId}:${toUserId}`;
    const messageId = await repo.insertMessage({
      accountId,
      contactId,
      wechatUserId: toUserId,
      direction: "outbound",
      msgType,
      content,
      mediaUrl: null,
      mediaType: null,
      wechatMsgId: null,
      contextToken: null,
      isRead: true,
      createdAt: Date.now(),
    });

    const resp: SendMessageResponse = { messageId };
    return reply.send(resp);
  });

  /** Mark messages as read */
  app.patch<{ Body: { contactId: string } }>("/api/messages/read", async (req, reply) => {
    const { contactId } = req.body;
    if (!contactId) return reply.status(400).send({ error: "contactId required" });
    await repo.markMessagesRead(contactId);
    await repo.markContactRead(contactId);
    return reply.send({ ok: true });
  });
}
