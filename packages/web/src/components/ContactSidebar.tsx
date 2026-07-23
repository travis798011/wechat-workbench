import type { Contact } from "@workbench/shared";

export default function ContactSidebar({ contact }: { contact: Contact }) {
  return (
    <div className="p-4">
      <div className="flex flex-col items-center mb-6">
        <div className="w-16 h-16 rounded-full bg-gray-300 dark:bg-gray-700 flex items-center justify-center text-xl font-bold mb-3">
          {contact.remark?.slice(0, 1) ?? contact.nickname?.slice(0, 1) ?? "?"}
        </div>
        <h3 className="font-medium">
          {contact.remark ?? contact.nickname ?? "未知用户"}
        </h3>
        {contact.remark && contact.nickname && (
          <p className="text-sm text-gray-500">昵称: {contact.nickname}</p>
        )}
      </div>

      <div className="space-y-4">
        <Section label="用户信息">
          <InfoRow label="微信 ID" value={contact.wechatUserId} />
          {contact.nickname && <InfoRow label="昵称" value={contact.nickname} />}
          <InfoRow
            label="最近消息"
            value={contact.lastMessage ?? "-"}
            truncate
          />
          <InfoRow
            label="最近时间"
            value={
              contact.lastMsgAt
                ? new Date(contact.lastMsgAt).toLocaleString("zh-CN")
                : "-"
            }
          />
        </Section>

        <Section label="状态">
          <InfoRow label="未读" value={String(contact.unreadCount)} />
          <InfoRow label="免打扰" value={contact.isMuted ? "是" : "否"} />
        </Section>
      </div>
    </div>
  );
}

function Section({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div>
      <h4 className="text-xs font-semibold text-gray-500 uppercase tracking-wider mb-2">
        {label}
      </h4>
      <div className="bg-gray-50 dark:bg-gray-900 rounded-lg p-3 space-y-2">
        {children}
      </div>
    </div>
  );
}

function InfoRow({
  label,
  value,
  truncate,
}: {
  label: string;
  value: string;
  truncate?: boolean;
}) {
  return (
    <div className="flex justify-between text-sm">
      <span className="text-gray-500">{label}</span>
      <span
        className={`max-w-[60%] text-right ${
          truncate ? "truncate" : "break-all"
        }`}
      >
        {value}
      </span>
    </div>
  );
}
