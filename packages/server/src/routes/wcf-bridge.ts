/**
 * WCF 聚合桥路由
 *
 * 作为 iLink 消息通道的替代/补充，接收 Python wcf-bridge 的消息和注册。
 *
 * 设计原则：
 * - "插入式" — 不修改现有 iLink 逻辑
 * - 新增路由独立文件
 * - 发送队列: 聚合桥轮询待发消息（HTTP polling，不需要双向 WS）
 */

import type { FastifyInstance } from "fastify";
import * as repo from "../db/repository.js";
import { bus } from "../ilink/event-bus.js";
import type { Message } from "@workbench/shared";

// ---- 发送队列（内存） ----

interface SendTask {
  id: string;
  accountWxid: string;
  toWxid: string;
  text: string;
  createdAt: number;
}

let sendQueue: SendTask[] = [];
let taskIdCounter = 0;

function nextTaskId(): string {
  return `send-${Date.now()}-${++taskIdCounter}`;
}

// ---- 路由 ----

export async function wcfRoutes(app: FastifyInstance) {
  /**
   * 聚合桥注册/更新账号
   * POST /api/wcf/account
   * Body: { wxid, name, label, port, status }
   */
  app.post("/api/wcf/account", async (req, reply) => {
    const body = req.body as Record<string, unknown>;
    const wxid = body.wxid as string;
    const name = (body.name as string) ?? wxid;
    const label = (body.label as string) ?? wxid;
    const status = (body.status as string) ?? "online";

    if (!wxid) {
      return reply.status(400).send({ error: "wxid required" });
    }

    // upsert account
    const now = Date.now();
    await repo.upsertAccount({
      id: wxid,
      label,
      botToken: "", // wcf 不需要 token
      baseUrl: "",
      status: status as any,
      avatar: null,
      nickname: name,
      wechatId: wxid,
      createdAt: now,
      updatedAt: now,
    });

    // broadcast status to web UI
    bus.emit("account_status", { accountId: wxid, status });

    return reply.send({ ok: true });
  });

  /**
   * 聚合桥推送消息
   * POST /api/wcf/message
   * Body: { account_wxid, from_wxid, from_name, msg_type, content, raw_type, ts }
   */
  app.post("/api/wcf/message", async (req, reply) => {
    const body = req.body as Record<string, unknown>;
    const {
      account_wxid: accountWxid,
      from_wxid: fromWxid,
      from_name: fromName,
      msg_type: msgType,
      content,
      raw_type: rawType,
      ts,
    } = body;

    if (!accountWxid || !fromWxid) {
      return reply.status(400).send({ error: "account_wxid and from_wxid required" });
    }

    const accountId = accountWxid as string;
    const contactId = `${accountId}:${fromWxid}`;
    const now = Date.now();

    // Upsert contact
    await repo.upsertContact({
      id: contactId,
      accountId,
      wechatUserId: fromWxid as string,
      nickname: (fromName as string) ?? null,
      avatar: null,
      remark: null,
      lastMessage: ((content as string) ?? "").slice(0, 100),
      lastMsgAt: now,
      unreadCount: 1,
      isMuted: false,
      createdAt: now,
      updatedAt: now,
    });

    // Update unread count (increment)
    await repo.updateContactUnread(contactId, 1);

    // Insert message
    const messageType = (msgType as string) ?? "text";
    const messageId = await repo.insertMessage({
      accountId,
      contactId,
      wechatUserId: fromWxid as string,
      direction: "inbound",
      msgType: messageType,
      content: (content as string) ?? null,
      mediaUrl: null,
      mediaType: null,
      wechatMsgId: null,
      contextToken: null,
      isRead: false,
      createdAt: (ts as number) ?? now,
    });

    const message: Message = {
      id: messageId,
      accountId,
      contactId,
      wechatUserId: fromWxid as string,
      direction: "inbound",
      msgType: messageType,
      content: (content as string) ?? null,
      mediaUrl: null,
      mediaType: null,
      wechatMsgId: null,
      contextToken: null,
      isRead: false,
      createdAt: (ts as number) ?? now,
    };

    // Push to websocket clients
    bus.emit("new_message", message);

    return reply.send({ ok: true, messageId });
  });

  /**
   * 聚合桥轮询发送队列
   * GET /api/wcf/send-queue?client=bridge-1
   * 返回该客户端待发送消息列表，取走即删除
   */
  app.get<{ Querystring: { client?: string } }>(
    "/api/wcf/send-queue",
    async (req, reply) => {
      const queue = [...sendQueue];
      sendQueue = [];
      return reply.send(queue);
    },
  );

  /**
   * 确认消息已发送
   * POST /api/wcf/send-ack
   * Body: { id: "send-xxx" }
   */
  app.post("/api/wcf/send-ack", async (req, reply) => {
    const body = req.body as { id?: string };
    if (body.id) {
      sendQueue = sendQueue.filter((t) => t.id !== body.id);
    }
    return reply.send({ ok: true });
  });

  // ---- WebSocket 发送拦截: 前端发消息时写入队列而非直接推 ----

  // 注意: ws/handler.ts 中的 send_message 处理逻辑会调用此方法
  // 我们在 app.ts 启动后注入一个 hook
}

/**
 * 向前端发送时使用的队列
 * 由 ws/handler.ts 中的 send_message 处理函数调用
 */
export function enqueueSend(
  accountWxid: string,
  toWxid: string,
  text: string,
): string {
  const task: SendTask = {
    id: nextTaskId(),
    accountWxid,
    toWxid,
    text,
    createdAt: Date.now(),
  };
  sendQueue.push(task);

  // 限制队列大小
  if (sendQueue.length > 1000) {
    sendQueue = sendQueue.slice(-500);
  }

  return task.id;
}
