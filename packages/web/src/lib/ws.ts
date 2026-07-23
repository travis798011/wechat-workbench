import type { WsServerMessage, WsClientMessage } from "@workbench/shared";

let ws: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let handlers: {
  onMessage?: (msg: WsServerMessage) => void;
  onAccountStatus?: (data: { accountId: string; status: string; message?: string }) => void;
} = {};

function connect() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
    return;
  }

  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  const url = `${protocol}//${location.host}/api/ws`;

  try {
    ws = new WebSocket(url);

    ws.onopen = () => {
      console.log("[WS] Connected");
    };

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data) as WsServerMessage;
        if (msg.type === "new_message") {
          handlers.onMessage?.(msg);
        } else if (msg.type === "account_status") {
          handlers.onAccountStatus?.(msg as any);
        }
      } catch { /* ignore bad json */ }
    };

    ws.onclose = () => {
      console.log("[WS] Disconnected, reconnecting in 3s...");
      ws = null;
      reconnectTimer = setTimeout(connect, 3000);
    };

    ws.onerror = (e) => {
      console.error("[WS] Error:", e);
      ws?.close();
    };
  } catch (e) {
    console.error("[WS] Connect failed:", e);
    reconnectTimer = setTimeout(connect, 5000);
  }
}

export function initWs(h: typeof handlers) {
  handlers = h;
  connect();
}

export function sendWs(msg: WsClientMessage) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(msg));
  }
}

export function subscribeWs(accountIds: string[]) {
  sendWs({ type: "subscribe", accountIds });
}

export function disconnectWs() {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  ws?.close();
  ws = null;
}
