/**
 * iLink HTTP 客户端 — 直接调用微信官方 API
 *
 * 所有函数都是纯函数，不依赖任何第三方 WeChat SDK。
 * 参考: weixin-agent-sdk 源码中的 api.ts + login-qr.ts + monitor.ts
 */

import crypto from "node:crypto";

// ---- Constants ----

export const API_BASE = "https://ilinkai.weixin.qq.com";
export const CDN_BASE = "https://novac2c.cdn.weixin.qq.com/c2c";
const BOT_TYPE = "3";
const AUTH_TYPE = "ilink_bot_token";

// ---- Helpers ----

function randomUin(): string {
  const buf = Buffer.alloc(4);
  crypto.randomFillSync(buf);
  return Buffer.from(String(buf.readUInt32BE(0)), "utf-8").toString("base64");
}

function buildHeaders(token?: string): Record<string, string> {
  const h: Record<string, string> = {
    "Content-Type": "application/json",
    AuthorizationType: AUTH_TYPE,
    "X-WECHAT-UIN": randomUin(),
  };
  if (token) h.Authorization = `Bearer ${token}`;
  return h;
}

async function apiGet(endpoint: string, timeoutMs: number): Promise<Record<string, unknown>> {
  const url = new URL(endpoint, API_BASE);
  const controller = new AbortController();
  const t = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetch(url.toString(), {
      method: "GET",
      headers: buildHeaders(),
      signal: controller.signal,
    });
    const text = await res.text();
    if (!res.ok) throw new Error(`HTTP ${res.status}: ${text.slice(0, 200)}`);
    return JSON.parse(text) as Record<string, unknown>;
  } finally {
    clearTimeout(t);
  }
}

async function apiPost(
  endpoint: string,
  body: Record<string, unknown>,
  token: string | undefined,
  timeoutMs: number,
): Promise<Record<string, unknown>> {
  const url = new URL(endpoint, API_BASE);
  const bodyStr = JSON.stringify(body);
  const headers = buildHeaders(token);
  headers["Content-Length"] = String(Buffer.byteLength(bodyStr));

  const controller = new AbortController();
  const t = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetch(url.toString(), {
      method: "POST",
      headers,
      body: bodyStr,
      signal: controller.signal,
    });
    const text = await res.text();
    if (!res.ok) throw new Error(`HTTP ${res.status}: ${text.slice(0, 200)}`);
    return JSON.parse(text) as Record<string, unknown>;
  } finally {
    clearTimeout(t);
  }
}

// ---- Login ----

export interface QrStartResult {
  qrcode: string;
  qrcodeUrl: string;
}

/** Step 1: request QR code from ilink */
export async function generateQR(): Promise<QrStartResult> {
  const resp = await apiGet(`ilink/bot/get_bot_qrcode?bot_type=${BOT_TYPE}`, 10_000);
  return {
    qrcode: resp.qrcode as string,
    qrcodeUrl: resp.qrcode_img_content as string,
  };
}

export type QrStatus = "wait" | "scaned" | "confirmed" | "expired" | "scaned_but_redirect";

export interface LoginResult {
  status: QrStatus;
  accountId?: string;
  botToken?: string;
  baseUrl?: string;
  userId?: string;
  redirectHost?: string;
}

/** Step 2: long-poll QR status until confirmed or expired. Returns null on timeout/error. */
export async function pollQR(qrcode: string, timeoutMs = 35_000): Promise<LoginResult> {
  try {
    const resp = await apiGet(
      `ilink/bot/get_qrcode_status?qrcode=${encodeURIComponent(qrcode)}`,
      timeoutMs,
    );
    return {
      status: (resp.status as QrStatus) ?? "wait",
      accountId: resp.ilink_bot_id as string,
      botToken: resp.bot_token as string,
      baseUrl: resp.baseurl as string,
      userId: resp.ilink_user_id as string,
      redirectHost: resp.redirect_host as string,
    };
  } catch (e) {
    if (e instanceof Error && e.name === "AbortError") {
      return { status: "wait" }; // timeout = normal
    }
    throw e;
  }
}

// ---- Messaging ----

export type WeixinMessageItem = {
  type?: number;
  text_item?: { text?: string };
  image_item?: {
    media?: { encrypt_query_param?: string; aes_key?: string; full_url?: string; encrypt_type?: number };
    aeskey?: string;
    url?: string;
  };
  voice_item?: {
    media?: { encrypt_query_param?: string; aes_key?: string; full_url?: string };
    encode_type?: number;
    text?: string;
  };
  file_item?: {
    media?: { encrypt_query_param?: string; aes_key?: string; full_url?: string };
    file_name?: string;
  };
  video_item?: {
    media?: { encrypt_query_param?: string; aes_key?: string; full_url?: string };
  };
};

export type WeixinMessage = {
  seq?: number;
  from_user_id?: string;
  to_user_id?: string;
  client_id?: string;
  create_time_ms?: number;
  session_id?: string;
  message_type?: number;
  item_list?: WeixinMessageItem[];
  context_token?: string;
};

export interface GetUpdatesResult {
  msgs: WeixinMessage[];
  syncBuf: string;
  /** server-suggested timeout for next poll */
  longPollTimeoutMs?: number;
}

/** Long-poll for new messages */
export async function getUpdates(
  token: string,
  syncBuf: string,
  timeoutMs = 35_000,
): Promise<GetUpdatesResult> {
  try {
    const resp = await apiPost(
      "ilink/bot/getupdates",
      {
        get_updates_buf: syncBuf,
        base_info: { channel_version: "workbench-0.1" },
      },
      token,
      timeoutMs + 5_000,
    );

    // Handle errors
    if (resp.ret && resp.ret !== 0) {
      const errcode = resp.errcode as number ?? resp.ret as number;
      throw new Error(`getUpdates error: ret=${resp.ret} errcode=${errcode}`);
    }

    return {
      msgs: (resp.msgs as WeixinMessage[]) ?? [],
      syncBuf: (resp.get_updates_buf as string) ?? syncBuf,
      longPollTimeoutMs: resp.longpolling_timeout_ms as number | undefined,
    };
  } catch (e) {
    if (e instanceof Error && e.name === "AbortError") {
      return { msgs: [], syncBuf }; // timeout = normal, no new messages
    }
    throw e;
  }
}

/** Send a text message */
export async function sendMessage(
  token: string,
  params: {
    toUserId: string;
    text: string;
    contextToken?: string;
  },
): Promise<void> {
  await apiPost(
    "ilink/bot/sendmessage",
    {
      base_info: { channel_version: "workbench-0.1" },
      msg: {
        from_user_id: "",
        to_user_id: params.toUserId,
        client_id: `wb:${Date.now()}-${crypto.randomBytes(4).toString("hex")}`,
        message_type: 2, // BOT
        message_state: 2, // FINISH
        item_list: params.text
          ? [{ type: 1, text_item: { text: params.text } }]
          : undefined,
        context_token: params.contextToken ?? undefined,
      },
    },
    token,
    15_000,
  );
}

/** Get bot config (includes typing ticket) */
export async function getConfig(
  token: string,
  ilinkUserId: string,
  contextToken?: string,
): Promise<{ typingTicket?: string }> {
  try {
    const resp = await apiPost(
      "ilink/bot/getconfig",
      {
        ilink_user_id: ilinkUserId,
        context_token: contextToken,
        base_info: { channel_version: "workbench-0.1" },
      },
      token,
      10_000,
    );
    return { typingTicket: resp.typing_ticket as string };
  } catch {
    return {};
  }
}
