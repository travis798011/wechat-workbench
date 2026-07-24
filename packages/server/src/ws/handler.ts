/**
 * WebSocket 会话管理 + 消息路由
 *
 * 每个前端连接维护一个订阅列表 (accountIds)。
 * 当 EventBus 发出 new_message 事件时，向所有订阅该账号的连接推送。
 */

import type { FastifyInstance } from "fastify";
import type { WebSocket } from "ws";
import { bus } from "../ilink/event-bus.js";
import { accountManager } from "../ilink/manager.js";
import * as repo from "../db/repository.js";
import { enqueueSend } from "../routes/wcf-bridge.js";
import type {
  WsClientMessage,
  WsServerMessage,
  Message,
} from "@workbench/shared";

// ---- Session tracking ----

interface WsSession {
  ws: WebSocket;
  accountIds: Set<string>;
}

const sessions = new Set<WsSession>();

function broadcastAccountStatus(accountId: string, status: string, message?: string) {
  const msg: WsServerMessage = {
    type: "account_status",
    accountId,
    status: status as WsServerMessage["status"],
    message,
  };
  for (const s of sessions) {
    if (s.accountIds.has(accountId) || s.accountIds.size === 0) {
      sendSafe(s.ws, msg);
    }
  }
}

function broadcastNewMessage(message: Message) {
  const msg: WsServerMessage = { type: "new_message", message };
  for (const s of sessions) {
    if (s.accountIds.has(message.accountId)) {
      sendSafe(s.ws, msg);
    }
  }
}

function sendSafe(ws: WebSocket, msg: WsServerMessage) {
  try {
    if (ws.readyState === ws.OPEN) {
      ws.send(JSON.stringify(msg));
    }
  } catch { /* ignore */ }
}

// ---- Setup ----

export async function wsRoutes(app: FastifyInstance) {
  const wsPlugin = await import("@fastify/websocket");
  await app.register(wsPlugin.default ?? wsPlugin);

  app.get("/api/ws", { websocket: true }, (socket, req) => {
    console.log("[WS] New connection");
    const session: WsSession = {
      ws: socket,
      accountIds: new Set(),
    };
    sessions.add(session);

    console.log(`[WS] Client connected (${sessions.size} active)`);

    socket.on("message", (raw) => {
      try {
        const data = JSON.parse(raw.toString()) as WsClientMessage;
        handleClientMessage(session, data).catch((e) => {
          console.error(`[WS] Handler error: ${e}`);
        });
      } catch (e) {
        sendSafe(socket, { type: "error", code: "PARSE_ERROR", message: String(e) });
      }
    });

    socket.on("close", () => {
      sessions.delete(session);
      console.log(`[WS] Client disconnected (${sessions.size} active)`);
    });

    socket.on("error", (e) => {
      console.error(`[WS] Socket error: ${e.message}`);
      sessions.delete(session);
    });
  });

  // ---- Subscribe to EventBus ----

  bus.on("new_message", (msg: Message) => {
    broadcastNewMessage(msg);
  });

  bus.on("account_status", (data: { accountId: string; status: string; message?: string }) => {
    broadcastAccountStatus(data.accountId, data.status, data.message);
  });
}

// ---- Client message handlers ----

async function handleClientMessage(session: WsSession, msg: WsClientMessage): Promise<void> {
  switch (msg.type) {
    case "subscribe": {
      session.accountIds = new Set(msg.accountIds);
      sendSafe(session.ws, {
        type: "account_status",
        accountId: "system",
        status: "online" as any,
        message: `Subscribed to ${msg.accountIds.length} accounts`,
      } as any);
      break;
    }

    case "send_message": {
      try {
        const acct = await repo.getAccount(msg.accountId);
        if (!acct) {
          sendSafe(session.ws, { type: "error", code: "ACCOUNT_NOT_FOUND", message: "Account not found" });
          return;
        }

        if (msg.msgType === "text") {
          // iLink Bot route: use SDK Bot.sendMessage() via manager
          if (acct.botToken && acct.baseUrl) {
            const bot = accountManager.getBot(msg.accountId);
            if (bot) {
              await bot.sendMessage(msg.content);
            } else {
              sendSafe(session.ws, { type: "error", code: "BOT_NOT_STARTED", message: "Bot not started, please re-login" });
              return;
            }
          } else {
            enqueueSend(msg.accountId, msg.toUserId, msg.content);
          }
        }

        // Store outbound
        const contactId = `${msg.accountId}:${msg.toUserId}`;
        const messageId = await repo.insertMessage({
          accountId: msg.accountId,
          contactId,
          wechatUserId: msg.toUserId,
          direction: "outbound",
          msgType: msg.msgType,
          content: msg.content,
          mediaUrl: null,
          mediaType: null,
          wechatMsgId: null,
          contextToken: null,
          isRead: true,
          createdAt: Date.now(),
        });

        sendSafe(session.ws, { type: "message_sent", messageId });
      } catch (e) {
        sendSafe(session.ws, {
          type: "error",
          code: "SEND_FAILED",
          message: `Send failed: ${String(e)}`,
        });
      }
      break;
    }

    case "mark_read": {
      await repo.markMessagesRead(msg.contactId);
      await repo.markContactRead(msg.contactId);
      break;
    }
  }
}
