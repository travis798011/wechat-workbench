import { useState, useEffect, useRef, useCallback } from "react";
import { api } from "../lib/api.ts";
import type { Message, SendMessageResponse } from "@workbench/shared";

export function useMessages(contactId: string | null) {
  const [messages, setMessages] = useState<Message[]>([]);
  const wsMessagesRef = useRef<Message[]>([]);

  // Sync ws-incoming messages with state
  wsMessagesRef.current = messages;

  // Load history when contact changes
  useEffect(() => {
    if (!contactId) {
      setMessages([]);
      return;
    }
    api
      .get<Message[]>(`/api/messages?contactId=${contactId}&limit=50`)
      .then(setMessages)
      .catch(console.error);
  }, [contactId]);

  // Append a new message from WebSocket
  const appendMessage = useCallback((msg: Message) => {
    setMessages((prev) => [...prev, msg]);
  }, []);

  const sendMessage = useCallback(
    async (
      accountId: string,
      toUserId: string,
      content: string,
      contextToken: string,
    ): Promise<void> => {
      if (!content.trim()) return;

      // Optimistic: add outbound message immediately
      const optimistic: Message = {
        id: -Date.now(),
        accountId,
        contactId: `${accountId}:${toUserId}`,
        wechatUserId: toUserId,
        direction: "outbound",
        msgType: "text",
        content,
        mediaUrl: null,
        mediaType: null,
        wechatMsgId: null,
        contextToken: null,
        isRead: true,
        createdAt: Date.now(),
      };
      setMessages((prev) => [...prev, optimistic]);

      try {
        const resp = await api.post<SendMessageResponse>("/api/messages/send", {
          accountId,
          toUserId,
          msgType: "text",
          content,
          contextToken,
        });
        // Replace optimistic with real id
        setMessages((prev) =>
          prev.map((m) =>
            m.id === optimistic.id ? { ...m, id: resp.messageId } : m,
          ),
        );
      } catch (e) {
        console.error("Send failed:", e);
        // Mark optimistic as failed
        setMessages((prev) =>
          prev.map((m) =>
            m.id === optimistic.id
              ? { ...m, content: `❌ 发送失败: ${content}` }
              : m,
          ),
        );
      }
    },
    [],
  );

  const markRead = useCallback(async (cid: string) => {
    try {
      await api.patch("/api/messages/read", { contactId: cid });
    } catch (e) {
      console.error("Mark read failed:", e);
    }
  }, []);

  return { messages, appendMessage, sendMessage, markRead };
}
