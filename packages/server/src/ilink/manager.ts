/**
 * 多账号 Monitor 管理器 — 基于 weixin-agent-sdk
 *
 * 用微信官方 SDK 替代自建的 iLink 通信层。
 * 消息存数据库并通过 EventBus 推 WebSocket。
 */

import { start, Bot } from "weixin-agent-sdk";
import * as repo from "../db/repository.js";
import { bus } from "./event-bus.js";
import type { Message, Contact } from "@workbench/shared";

type MonitorStatus = "idle" | "running" | "error" | "stopped";

class AccountManager {
  private bots = new Map<string, Bot>();
  private _statuses = new Map<string, MonitorStatus>();

  async boot(): Promise<void> {
    const accounts = await repo.listAccounts();
    for (const acct of accounts) {
      if (acct.status === "online") {
        console.log(`[Manager] Resuming bot for ${acct.id}`);
        this.startBot(acct.id);
      }
    }
    console.log(`[Manager] Booted ${this.bots.size} bots.`);
  }

  async startBot(accountId: string): Promise<void> {
    this.stopBot(accountId);

    try {
      // SDK start() reads credentials from ~/.openclaw/weixin/accounts/
      // which were saved by login(). We just need to call start().
      const bot = start(createAgent(accountId), {
        accountId, // normalized ID
        log: (msg: string) => console.log(`[Bot:${accountId}] ${msg}`),
      });
      this.bots.set(accountId, bot);
      this._statuses.set(accountId, "running");
      console.log(`[Manager] Bot started: ${accountId}`);
      bus.emit("account_status", { accountId, status: "online" });
    } catch (e) {
      console.error(`[Manager] Failed to start bot ${accountId}:`, e);
      this._statuses.set(accountId, "error");
      bus.emit("account_status", { accountId, status: "error", message: String(e) });
    }
  }

  stopBot(accountId: string): void {
    // SDK doesn't provide a stop() on Bot
    // The monitor will stop when token expires or we don't hold a ref
    this.bots.delete(accountId);
    this._statuses.delete(accountId);
    console.log(`[Manager] Bot stopped: ${accountId}`);
    bus.emit("account_status", { accountId, status: "offline" });
  }

  async shutdown(): Promise<void> {
    this.bots.clear();
    this._statuses.clear();
    console.log("[Manager] All bots stopped.");
  }

  isRunning(accountId: string): boolean {
    return this._statuses.get(accountId) === "running";
  }

  getBot(accountId: string): Bot | undefined {
    return this.bots.get(accountId);
  }
}

function createAgent(accountId: string) {
  return {
    chat: async (request: { conversationId: string; text: string; media?: any }) => {
      const { conversationId, text, media } = request;
      console.log(`[Bot:${accountId}] Message from ${conversationId}: ${text.slice(0, 100)}`);

      // Upsert contact
      const contactId = `${accountId}:${conversationId}`;
      const now = Date.now();
      await repo.upsertContact({
        id: contactId,
        accountId,
        wechatUserId: conversationId,
        nickname: conversationId,
        avatar: null,
        remark: null,
        lastMessage: text?.slice(0, 100) ?? `[${media?.type ?? "unknown"}]`,
        lastMsgAt: now,
        unreadCount: 0, // reset each time, then increment
        isMuted: false,
        createdAt: now,
        updatedAt: now,
      });
      await repo.updateContactUnread(contactId, 1);

      // Insert message
      const messageId = await repo.insertMessage({
        accountId,
        contactId,
        wechatUserId: conversationId,
        direction: "inbound",
        msgType: "text",
        content: text ?? null,
        mediaUrl: media?.filePath ?? null,
        mediaType: media?.mimeType ?? null,
        wechatMsgId: null,
        contextToken: null,
        isRead: false,
        createdAt: now,
      });

      const message: Message = {
        id: messageId,
        accountId,
        contactId,
        wechatUserId: conversationId,
        direction: "inbound",
        msgType: "text",
        content: text ?? null,
        mediaUrl: media?.filePath ?? null,
        mediaType: media?.mimeType ?? null,
        wechatMsgId: null,
        contextToken: null,
        isRead: false,
        createdAt: now,
      };

      bus.emit("new_message", message);

      // Return empty response — we don't want AI auto-reply
      return {};
    },

    clearSession: (_conversationId: string) => {
      // No-op: we don't maintain sessions
    },
  };
}

export const accountManager = new AccountManager();
