// ---- Client → Server ----

export interface WsClientMessageSubscribe {
  type: "subscribe";
  accountIds: string[];
}

export interface WsClientMessageSend {
  type: "send_message";
  accountId: string;
  toUserId: string;
  msgType: "text";
  content: string;
  contextToken: string;
}

export interface WsClientMessageMarkRead {
  type: "mark_read";
  contactId: string;
}

export interface WsClientMessageTyping {
  type: "typing";
  accountId: string;
  toUserId: string;
}

export type WsClientMessage =
  | WsClientMessageSubscribe
  | WsClientMessageSend
  | WsClientMessageMarkRead
  | WsClientMessageTyping;

// ---- Server → Client ----

export interface WsServerMessageNewMessage {
  type: "new_message";
  message: import("./types.js").Message;
}

export interface WsServerMessageSent {
  type: "message_sent";
  messageId: number;
  clientMsgId?: string;
}

export interface WsServerMessageAccountStatus {
  type: "account_status";
  accountId: string;
  status: import("./types.js").AccountStatus;
  message?: string;
}

export interface WsServerMessageError {
  type: "error";
  code: string;
  message: string;
}

export type WsServerMessage =
  | WsServerMessageNewMessage
  | WsServerMessageSent
  | WsServerMessageAccountStatus
  | WsServerMessageError;
