import type { Message } from "@workbench/shared";

export default function MessageBubble({ message }: { message: Message }) {
  const isInbound = message.direction === "inbound";

  return (
    <div className={`flex ${isInbound ? "justify-start" : "justify-end"}`}>
      <div
        className={`max-w-[75%] px-3 py-2 rounded-lg text-sm leading-relaxed ${
          isInbound
            ? "bg-white dark:bg-gray-800 rounded-bl-sm"
            : "bg-wechat-green text-white rounded-br-sm"
        }`}
      >
        {message.msgType === "text" ? (
          <span className="whitespace-pre-wrap break-words">
            {message.content}
          </span>
        ) : (
          <span className="italic opacity-75">{message.content}</span>
        )}
        <div
          className={`text-[10px] mt-1 ${
            isInbound ? "text-gray-400" : "text-green-100"
          }`}
        >
          {new Date(message.createdAt).toLocaleTimeString("zh-CN", {
            hour: "2-digit",
            minute: "2-digit",
          })}
        </div>
      </div>
    </div>
  );
}
