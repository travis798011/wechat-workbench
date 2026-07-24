// ---- Account ----

export type AccountStatus = "offline" | "qr_pending" | "scanning" | "online" | "error";

export interface Account {
  id: string;           // ilink_bot_id
  label: string;        // display name
  botToken: string;     // encrypted
  baseUrl: string;
  status: AccountStatus;
  avatar: string | null;
  nickname: string | null;
  wechatId: string | null;
  createdAt: number;
  updatedAt: number;
}

export type AccountSummary = Omit<Account, "botToken" | "baseUrl">;

// ---- Contact ----

export interface Contact {
  id: string;           // "{accountId}:{wechatUserId}"
  accountId: string;
  wechatUserId: string;
  nickname: string | null;
  avatar: string | null;
  remark: string | null;
  lastMessage: string | null;
  lastMsgAt: number;
  unreadCount: number;
  isMuted: boolean;
  createdAt: number;
  updatedAt: number;
}

// ---- Message ----

export type MessageDirection = "inbound" | "outbound";
export type MessageType = "text" | "image" | "voice" | "file" | "video";

export interface Message {
  id: number;
  accountId: string;
  contactId: string;
  wechatUserId: string;
  direction: MessageDirection;
  msgType: MessageType;
  content: string | null;
  mediaUrl: string | null;
  mediaType: string | null;
  wechatMsgId: string | null;
  contextToken: string | null;
  isRead: boolean;
  createdAt: number;
}

// ---- API request/response ----

export interface LoginStartResponse {
  qrcode: string;         // qrcode value for polling
  qrcodeUrl: string;      // weixin liteapp URL
}

export interface LoginStatusResponse {
  status: "wait" | "scaned" | "confirmed" | "expired";
  accountId?: string;
  botToken?: string;
  userId?: string;
  baseUrl?: string;
}

export interface SendMessageRequest {
  accountId: string;
  toUserId: string;
  msgType: MessageType;
  content: string;
  contextToken: string;
}

export interface SendMessageResponse {
  messageId: number;
}

export interface MessagesQuery {
  contactId?: string;
  accountId?: string;
  before?: number;   // cursor (message id)
  limit?: number;    // default 50
}

export interface ContactsQuery {
  accountId?: string;
}
