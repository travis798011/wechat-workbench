import type { ReactNode } from "react";

export default function Layout({
  left,
  center,
  right,
  children,
}: {
  left: ReactNode;
  center: ReactNode;
  right: ReactNode;
  children?: ReactNode;
}) {
  return (
    <div className="flex h-full">
      {/* Left sidebar: accounts */}
      <aside className="w-64 bg-sidebar text-white flex-shrink-0 flex flex-col">
        {left}
      </aside>

      {/* Center: chat window */}
      <main className="flex-1 flex flex-col min-w-0">
        {center}
      </main>

      {/* Right sidebar: contact details */}
      {right && (
        <aside className="w-72 border-l border-gray-200 dark:border-gray-800 flex-shrink-0 overflow-y-auto">
          {right}
        </aside>
      )}

      {/* Overlay (login panel, etc) */}
      {children}
    </div>
  );
}
