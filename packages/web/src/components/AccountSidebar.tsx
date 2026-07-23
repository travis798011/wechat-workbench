import type { AccountSummary } from "@workbench/shared";

const statusColors: Record<string, string> = {
  online: "bg-green-500",
  qr_pending: "bg-yellow-500",
  scanning: "bg-blue-500",
  error: "bg-red-500",
  offline: "bg-gray-400",
};

const statusLabels: Record<string, string> = {
  online: "在线",
  qr_pending: "等待扫码",
  scanning: "扫码中",
  error: "异常",
  offline: "离线",
};

export default function AccountSidebar({
  accounts,
  selectedId,
  onSelect,
  onAdd,
  onRemove,
}: {
  accounts: AccountSummary[];
  selectedId: string | null;
  onSelect: (id: string) => void;
  onAdd: () => void;
  onRemove: (id: string) => void;
}) {
  return (
    <div className="flex flex-col h-full">
      <div className="p-4 border-b border-gray-700">
        <h1 className="text-lg font-bold">微信客服工作台</h1>
        <p className="text-xs text-gray-400 mt-1">
          {accounts.length} 个账号
        </p>
      </div>

      <div className="flex-1 overflow-y-auto">
        {accounts.map((acct) => (
          <div
            key={acct.id}
            className={`flex items-center gap-3 px-4 py-3 cursor-pointer transition-colors ${
              selectedId === acct.id
                ? "bg-sidebar-active"
                : "hover:bg-sidebar-hover"
            }`}
            onClick={() => onSelect(acct.id)}
          >
            <div className="relative">
              <div className="w-10 h-10 rounded-full bg-gray-600 flex items-center justify-center text-sm font-bold">
                {acct.label?.slice(0, 2).toUpperCase() ?? "?"}
              </div>
              <span
                className={`absolute -bottom-0.5 -right-0.5 w-3.5 h-3.5 rounded-full border-2 border-sidebar ${
                  statusColors[acct.status] ?? statusColors.offline
                }`}
                title={statusLabels[acct.status] ?? acct.status}
              />
            </div>
            <div className="flex-1 min-w-0">
              <div className="text-sm font-medium truncate">
                {acct.nickname ?? acct.label}
              </div>
              <div className="text-xs text-gray-400">
                {statusLabels[acct.status] ?? acct.status}
              </div>
            </div>
            <button
              className="text-gray-500 hover:text-red-400 text-xs px-1"
              onClick={(e) => {
                e.stopPropagation();
                if (confirm("确认删除此账号?")) onRemove(acct.id);
              }}
              title="删除"
            >
              ✕
            </button>
          </div>
        ))}
      </div>

      <div className="p-3 border-t border-gray-700">
        <button
          onClick={onAdd}
          className="w-full py-2 text-sm bg-wechat-green hover:bg-green-600 text-white rounded-lg transition-colors"
        >
          + 添加微信账号
        </button>
      </div>
    </div>
  );
}
