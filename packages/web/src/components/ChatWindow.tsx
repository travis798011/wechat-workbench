import { useState, useRef, useEffect } from "react";
import type { Message, Contact } from "@workbench/shared";
import MessageBubble from "./MessageBubble.tsx";

export default function ChatWindow({
  contact,
  messages,
  onSend,
}: {
  contact: Contact;
  messages: Message[];
  onSend: (text: string) => Promise<void>;
}) {
  const [input, setInput] = useState("");
  const [sending, setSending] = useState(false);
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages]);

  const handleSubmit = async () => {
    const text = input.trim();
    if (!text || sending) return;
    setSending(true);
    try {
      await onSend(text);
      setInput("");
    } finally {
      setSending(false);
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      handleSubmit();
    }
  };

  return (
    <div className="flex flex-col h-full">
      {/* Chat header */}
      <div className="flex items-center gap-3 px-4 py-3 border-b border-gray-200 dark:border-gray-800 bg-white dark:bg-gray-900">
        <div className="w-9 h-9 rounded-full bg-gray-300 dark:bg-gray-700 flex items-center justify-center text-sm font-bold">
          {contact.remark?.slice(0, 1) ?? contact.nickname?.slice(0, 1) ?? "?"}
        </div>
        <div>
          <div className="font-medium text-sm">
            {contact.remark ?? contact.nickname ?? contact.wechatUserId}
          </div>
          <div className="text-xs text-gray-500">{contact.wechatUserId}</div>
        </div>
      </div>

      {/* Messages */}
      <div className="flex-1 overflow-y-auto px-4 py-3 space-y-3 bg-wechat-bg dark:bg-gray-950">
        {messages.length === 0 && (
          <div className="text-center text-gray-400 text-sm mt-8">
            暂无消息
          </div>
        )}
        {messages.map((msg) => (
          <MessageBubble key={msg.id} message={msg} />
        ))}
        <div ref={bottomRef} />
      </div>

      {/* Input */}
      <div className="flex gap-2 p-3 border-t border-gray-200 dark:border-gray-800 bg-white dark:bg-gray-900">
        <input
          type="text"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="输入消息... (Enter 发送)"
          className="flex-1 px-3 py-2 border border-gray-300 dark:border-gray-700 rounded-lg bg-gray-50 dark:bg-gray-800 text-sm focus:outline-none focus:ring-2 focus:ring-wechat-green"
        />
        <button
          onClick={handleSubmit}
          disabled={sending || !input.trim()}
          className="px-5 py-2 bg-wechat-green hover:bg-green-600 disabled:opacity-50 text-white text-sm rounded-lg transition-colors"
        >
          {sending ? "..." : "发送"}
        </button>
      </div>
    </div>
  );
}
