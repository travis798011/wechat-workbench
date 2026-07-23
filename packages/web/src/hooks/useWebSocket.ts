import { useState, useEffect, useRef, useCallback } from "react";
import { initWs, disconnectWs, subscribeWs, sendWs } from "../lib/ws.ts";
import type { WsServerMessage } from "@workbench/shared";

export function useWs(options: {
  onMessage?: (msg: WsServerMessage) => void;
  onAccountStatus?: (data: { accountId: string; status: string; message?: string }) => void;
}) {
  const [connected, setConnected] = useState(true);
  const optionsRef = useRef(options);
  optionsRef.current = options;

  useEffect(() => {
    initWs({
      onMessage: (msg) => optionsRef.current.onMessage?.(msg),
      onAccountStatus: (data) => optionsRef.current.onAccountStatus?.(data),
    });
    return () => disconnectWs();
  }, []);

  const subscribe = useCallback((accountIds: string[]) => {
    subscribeWs(accountIds);
  }, []);

  return { connected, subscribe };
}
