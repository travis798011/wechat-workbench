import type { FastifyInstance } from "fastify";
import * as repo from "../db/repository.js";
import { accountManager } from "../ilink/manager.js";
import { enqueueSend } from "./wcf-bridge.js";
import type { SendMessageRequest, SendMessageResponse, MessagesQuery } from "@workbench/shared";

const ILINK_API = "https://ilinkai.weixin.qq.com";

/** Direct iLink send — doesn't need SDK context_token */
async function ilinkSendText(token: string, toUserId: string, text: string): Promise<void> {
  const body = {
    base_info: { channel_version: "workbench-0.1" },
    msg: {
      from_user_id: "",
      to_user_id: toUserId,
      client_id: `wb:${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
      message_type: 2,
      message_state: 2,
      item_list: [{ type: 1, text_item: { text } }],
    },
  };
  const res = await fetch(`${ILINK_API}/ilink/bot/sendmessage`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      AuthorizationType: "ilink_bot_token",
      Authorization: `Bearer ${token}`,
      "X-WECHAT-UIN": Buffer.from(token).toString("base64").slice(0, 16),
    },
    body: JSON.stringify(body),
  });
  const data = await res.json() as any;
  if (data.errcode !== undefined && data.errcode !== 0) {
    throw new Error(`iLink errcode=${data.errcode} ${data.errmsg ?? ""}`);
  }
}

export async function messageRoutes(app: FastifyInstance) {
  /** Get message history for a contact */
  app.get<{ Querystring: MessagesQuery }>("/api/messages", async (req, reply) => {
    const { contactId, accountId, before, limit } = req.query;
    if (!contactId && !accountId) return reply.status(400).send({ error: "contactId or accountId is required" });
    if (contactId) {
      const msgs = await repo.listMessages(contactId, before ? Number(before) : undefined, limit ? Number(limit) : 50);
      return reply.send(msgs);
    }
    // By accountId: get all messages for this account
    const msgs = await repo.listMessagesByAccount(accountId!, before ? Number(before) : undefined, limit ? Number(limit) : 100);
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

    if (acct.botToken && acct.baseUrl) {
      // Direct iLink send — no SDK context_token needed
      try {
        if (msgType === "text") {
          await ilinkSendText(acct.botToken, toUserId, content);
        }
      } catch (e) {
        return reply.status(500).send({ error: `Send failed: ${String(e)}` });
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
