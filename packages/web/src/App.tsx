import { useState, useCallback } from "react";
import Layout from "./components/Layout.tsx";
import AccountSidebar from "./components/AccountSidebar.tsx";
import ChatWindow from "./components/ChatWindow.tsx";
import ContactSidebar from "./components/ContactSidebar.tsx";
import LoginPanel from "./components/LoginPanel.tsx";
import { useWs } from "./hooks/useWebSocket.ts";
import { useAccounts } from "./hooks/useAccounts.ts";
import { useMessages } from "./hooks/useMessages.ts";
import type { Contact } from "@workbench/shared";

export default function App() {
  const [selectedAccountId, setSelectedAccountId] = useState<string | null>(null);
  const [selectedContact, setSelectedContact] = useState<Contact | null>(null);
  const [showLogin, setShowLogin] = useState(false);

  const { accounts, addAccount, removeAccount } = useAccounts();
  const { messages, sendMessage, markRead } = useMessages(selectedContact?.id ?? null);
  const { connected, subscribe } = useWs({
    onMessage: (msg) => {
      // Messages are handled by useMessages hook
    },
    onAccountStatus: (data) => {
      // Status handled by useAccounts hook
    },
  });

  // Subscribe to selected account for real-time updates
  const handleSelectAccount = useCallback(
    (accountId: string) => {
      setSelectedAccountId(accountId);
      setSelectedContact(null);
      subscribe([accountId]);
    },
    [subscribe],
  );

  const handleSelectContact = useCallback((contact: Contact) => {
    setSelectedContact(contact);
    markRead(contact.id);
  }, [markRead]);

  const handleSend = useCallback(
    async (text: string) => {
      if (!selectedAccountId || !selectedContact) return;
      // Get the latest inbound message's context token
      const lastMsg = messages.find((m) => m.direction === "inbound");
      await sendMessage(
        selectedAccountId,
        selectedContact.wechatUserId,
        text,
        lastMsg?.contextToken ?? "",
      );
    },
    [selectedAccountId, selectedContact, messages, sendMessage],
  );

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
        selectedContact ? (
          <ChatWindow
            contact={selectedContact}
            messages={messages}
            onSend={handleSend}
          />
        ) : (
          <div className="flex items-center justify-center h-full text-gray-400">
            {selectedAccountId
              ? "选择一个联系人或等待新消息"
              : "选择一个账号开始"}
          </div>
        )
      }
      right={
        selectedContact ? (
          <ContactSidebar contact={selectedContact} />
        ) : null
      }
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
