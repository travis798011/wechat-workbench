/**
 * 单账号消息监听器
 *
 * 每个账号一个 Monitor 实例，运行独立的 long-poll 循环。
 * 收到消息后写入数据库并通过 EventBus 推送。
 */

import * as ilink from "./client.js";
import type { WeixinMessage, WeixinMessageItem } from "./client.js";
import * as repo from "../db/repository.js";
import { bus } from "./event-bus.js";
import type { Message, Contact, MessageType } from "@workbench/shared";

export type MonitorStatus = "idle" | "running" | "error" | "stopped";

export class AccountMonitor {
  readonly accountId: string;
  private token: string;
  private _status: MonitorStatus = "idle";
  private abortController: AbortController | null = null;
  private longPollTimeoutMs = 35_000;
  private errorCount = 0;
  private maxErrors = 5;

  constructor(accountId: string, token: string) {
    this.accountId = accountId;
    this.token = token;
  }

  get status(): MonitorStatus { return this._status; }

  updateToken(token: string) { this.token = token; }

  async start(): Promise<void> {
    if (this._status === "running") return;
    this._status = "running";
    this.errorCount = 0;
    this.abortController = new AbortController();

    console.log(`[Monitor:${this.accountId}] Starting...`);
    bus.emit("account_status", { accountId: this.accountId, status: "online" });
    await repo.updateAccountStatus(this.accountId, "online");

    // Load saved syncBuf
    let syncBuf = await repo.getSyncBuf(this.accountId);

    const loop = async () => {
      console.log(`[Monitor:${this.accountId}] Loop started`);
      let iteration = 0;
      while (this._status === "running" && !this.abortController!.signal.aborted) {
        iteration++;
        try {
          console.log(`[Monitor:${this.accountId}] Iteration ${iteration}, longPollTimeout=${this.longPollTimeoutMs}ms`);
          const result = await ilink.getUpdates(this.token, syncBuf, this.longPollTimeoutMs);
          console.log(`[Monitor:${this.accountId}] getUpdates returned: msgs=${result.msgs.length}, syncBuf=${result.syncBuf.length}`);

          // Update suggested timeout
          if (result.longPollTimeoutMs && result.longPollTimeoutMs > 0) {
            this.longPollTimeoutMs = result.longPollTimeoutMs;
          }

          this.errorCount = 0;

          if (result.syncBuf && result.syncBuf !== syncBuf) {
            syncBuf = result.syncBuf;
            await repo.updateSyncBuf(this.accountId, syncBuf);
          }

          // Process messages
          for (const msg of result.msgs) {
            await this.processMessage(msg);
          }
        } catch (e) {
          if (this.abortController?.signal.aborted) break;
          this.errorCount++;
          console.error(`[Monitor:${this.accountId}] Error in iteration ${iteration} (#${this.errorCount}):`, e instanceof Error ? e.message : String(e));
          console.error(`[Monitor:${this.accountId}] Stack:`, e instanceof Error ? e.stack : 'no stack');
          if (this.errorCount >= this.maxErrors) {
            console.error(`[Monitor:${this.accountId}] Too many errors, stopping.`);
            this._status = "error";
            bus.emit("account_status", { accountId: this.accountId, status: "error", message: `连续 ${this.maxErrors} 次错误` });
            await repo.updateAccountStatus(this.accountId, "error");
            return;
          }
          // Backoff
          const delay = Math.min(2000 * Math.pow(2, this.errorCount - 1), 30_000);
          await new Promise((r) => setTimeout(r, delay));
        }
      }
    };

    loop().catch((e) => {
      console.error(`[Monitor:${this.accountId}] Fatal: ${e}`);
      this._status = "error";
      bus.emit("account_status", { accountId: this.accountId, status: "error", message: String(e) });
    });
  }

  async stop(): Promise<void> {
    this._status = "stopped";
    this.abortController?.abort();
    this.abortController = null;
    console.log(`[Monitor:${this.accountId}] Stopped.`);
    // Don't change DB status on shutdown — the account stays "online"
    // and will be recovered on next boot
    bus.emit("account_status", { accountId: this.accountId, status: "offline" });
  }

  // ---- Private: message processing ----

  private async processMessage(msg: WeixinMessage): Promise<void> {
    const fromUserId = msg.from_user_id ?? "";
    const contactId = `${this.accountId}:${fromUserId}`;

    if (!fromUserId) return;

    for (const item of msg.item_list ?? []) {
      const { msgType, content, mediaUrl, mediaType } = this.parseItem(item);

      // Skip empty messages
      if (!content && !mediaUrl) continue;

      const contact: Contact = {
        id: contactId,
        accountId: this.accountId,
        wechatUserId: fromUserId,
        nickname: null,
        avatar: null,
        remark: null,
        lastMessage: content?.slice(0, 100) ?? `[${msgType}]`,
        lastMsgAt: msg.create_time_ms ?? Date.now(),
        unreadCount: 0,
        isMuted: false,
        createdAt: Date.now(),
        updatedAt: Date.now(),
      };
      await repo.upsertContact(contact);
      await repo.updateContactUnread(contactId, 1);

      const message: Omit<Message, "id"> = {
        accountId: this.accountId,
        contactId,
        wechatUserId: fromUserId,
        direction: "inbound",
        msgType,
        content: content ?? null,
        mediaUrl: mediaUrl ?? null,
        mediaType: mediaType ?? null,
        wechatMsgId: item.msg_id ?? null,
        contextToken: msg.context_token ?? null,
        isRead: false,
        createdAt: msg.create_time_ms ?? Date.now(),
      };

      const messageId = await repo.insertMessage(message);
      const fullMessage: Message = { ...message, id: messageId };

      // Push via EventBus → WebSocket
      bus.emit("new_message", fullMessage);
    }
  }

  private parseItem(item: WeixinMessageItem): {
    msgType: MessageType;
    content?: string;
    mediaUrl?: string;
    mediaType?: string;
  } {
    const type = item.type ?? 0;
    switch (type) {
      case 1: // TEXT
        return { msgType: "text", content: item.text_item?.text ?? "" };
      case 2: // IMAGE
        return {
          msgType: "image",
          content: "[图片]",
          mediaUrl: item.image_item?.url ?? (item.image_item?.media?.full_url),
          mediaType: "image/*",
        };
      case 3: // VOICE
        return {
          msgType: "voice",
          content: item.voice_item?.text ? `[语音→文字] ${item.voice_item.text}` : "[语音]",
        };
      case 4: // FILE
        return {
          msgType: "file",
          content: `[文件] ${item.file_item?.file_name ?? ""}`,
        };
      case 5: // VIDEO
        return {
          msgType: "video",
          content: "[视频]",
          mediaUrl: item.video_item?.media?.full_url,
          mediaType: "video/mp4",
        };
      default:
        return { msgType: "text", content: `[未知类型 ${type}]` };
    }
  }
}
