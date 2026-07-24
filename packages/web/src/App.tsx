import { useState, useEffect, useRef } from "react";
import Layout from "./components/Layout.tsx";
import AccountSidebar from "./components/AccountSidebar.tsx";
import MessageBubble from "./components/MessageBubble.tsx";
import ContactSidebar from "./components/ContactSidebar.tsx";
import LoginPanel from "./components/LoginPanel.tsx";
import { useWs } from "./hooks/useWebSocket.ts";
import { useAccounts } from "./hooks/useAccounts.ts";
import { api } from "./lib/api.ts";
import type { Message, Contact } from "@workbench/shared";

export default function App() {
  const [selectedAccountId, setSelectedAccountId] = useState<string | null>(null);
  const [selectedContact] = useState<Contact | null>(null);
  const [showLogin, setShowLogin] = useState(false);
  const [messages, setMessages] = useState<Message[]>([]);
  const msgEndRef = useRef<HTMLDivElement>(null);

  // Auto-scroll to bottom when messages change
  useEffect(() => {
    msgEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages]);

  const { accounts, addAccount, removeAccount } = useAccounts();
  const { subscribe } = useWs({
    onMessage: (msg) => {
      if (msg.type === "new_message") {
        setMessages((prev) => [...prev, msg.message]);
      }
    },
  });

  useEffect(() => {
    if (!selectedAccountId) {
      setMessages([]);
      return;
    }
    api
      .get<Message[]>(`/api/messages?accountId=${selectedAccountId}&limit=100`)
      .then(setMessages)
      .catch(console.error);
    // Poll for new messages every 3 seconds
    const interval = setInterval(() => {
      api
        .get<Message[]>(`/api/messages?accountId=${selectedAccountId}&limit=100`)
        .then(setMessages)
        .catch(console.error);
    }, 3000);
    return () => clearInterval(interval);
  }, [selectedAccountId]);

  const handleSelectAccount = (accountId: string) => {
    setSelectedAccountId(accountId);
    subscribe([accountId]);
  };

  const handleSend = async () => {
    const el = document.getElementById("msg-input") as HTMLInputElement;
    const text = el?.value?.trim();
    if (!text || !selectedAccountId) return;

    // Use the last message's sender, or the bot's own WeChat ID as fallback
    const lastMsg = [...messages].reverse().find((m) => m.wechatUserId && m.contactId);
    const toUserId = lastMsg?.wechatUserId ?? selectedAccountId;
    const contactId = lastMsg?.contactId ?? `${selectedAccountId}:${selectedAccountId}`;

    try {
      await api.post("/api/messages/send", {
        accountId: selectedAccountId,
        toUserId,
        msgType: "text" as const,
        content: text,
        contextToken: lastMsg?.contextToken ?? "",
      });
      setMessages((prev) => [
        ...prev,
        {
          id: -Date.now(),
          accountId: selectedAccountId,
          contactId,
          wechatUserId: toUserId,
          direction: "outbound",
          msgType: "text",
          content: text,
          mediaUrl: null,
          mediaType: null,
          wechatMsgId: null,
          contextToken: null,
          isRead: true,
          createdAt: Date.now(),
        },
      ]);
      el.value = "";
    } catch (e) {
      console.error("Send error:", e);
      alert("发送失败: " + String(e));
    }
  };

  return (
    <Layout
      left={
        <AccountSidebar
          accounts={accounts}
          selectedId={selectedAccountId}
          onSelect={handleSelectAccount}
          onAdd={() => setShowLogin(true)}
          onRemove={removeAccount}
        />
      }
      center={
        !selectedAccountId ? (
          <div className="flex items-center justify-center h-full text-gray-400">
            选择一个账号开始
          </div>
        ) : (
          <div className="flex flex-col h-full">
            <div className="flex-1 overflow-y-auto px-4 py-3 space-y-3 bg-wechat-bg dark:bg-gray-950">
              {messages.length === 0 && (
                <div className="text-center text-gray-400 text-sm mt-8">
                  暂无消息
                </div>
              )}
              {messages.map((msg) => (
                <MessageBubble key={msg.id} message={msg} />
              ))}
              <div ref={msgEndRef} />
            </div>
            <div className="flex gap-2 p-3 border-t border-gray-200 dark:border-gray-800 bg-white dark:bg-gray-900">
              <input
                id="msg-input"
                type="text"
                onKeyDown={(e) => {
                  if (e.key === "Enter" && !e.shiftKey) {
                    e.preventDefault();
                    handleSend();
                  }
                }}
                placeholder="回复消息... (Enter 发送)"
                className="flex-1 px-3 py-2 border border-gray-300 dark:border-gray-700 rounded-lg bg-gray-50 dark:bg-gray-800 text-sm focus:outline-none focus:ring-2 focus:ring-wechat-green"
              />
              <button
                onClick={handleSend}
                className="px-5 py-2 bg-wechat-green hover:bg-green-600 text-white text-sm rounded-lg transition-colors"
              >
                发送
              </button>
            </div>
          </div>
        )
      }
      right={selectedContact ? <ContactSidebar contact={selectedContact} /> : null}
    >
      {showLogin && (
        <LoginPanel
          onClose={() => setShowLogin(false)}
          onSuccess={(acct) => {
            addAccount(acct);
            setShowLogin(false);
          }}
        />
      )}
    </Layout>
  );
}
