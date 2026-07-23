import { useState, useEffect, useRef } from "react";
import { api } from "../lib/api.ts";
import type { LoginStartResponse, AccountSummary } from "@workbench/shared";

export default function LoginPanel({
  onClose,
  onSuccess,
}: {
  onClose: () => void;
  onSuccess: (acct: AccountSummary) => void;
}) {
  const [step, setStep] = useState<"idle" | "loading" | "qrcode" | "scanning" | "done" | "error">("idle");
  const [qrUrl, setQrUrl] = useState("");
  const [error, setError] = useState("");
  const [label, setLabel] = useState("");
  const pollingRef = useRef<AbortController | null>(null);

  const startLogin = async () => {
    const id = label.trim() || `acct-${Date.now()}`;
    setStep("loading");
    setError("");
    try {
      const resp = await api.post<LoginStartResponse>(`/api/accounts/${id}/login`);
      setQrUrl(resp.qrcodeUrl);
      setStep("qrcode");

      // Start polling status
      pollingRef.current?.abort();
      const ctrl = new AbortController();
      pollingRef.current = ctrl;

      while (!ctrl.signal.aborted) {
        await new Promise((r) => setTimeout(r, 2000));
        try {
          const status = await api.get<any>(`/api/accounts/${id}/login/status`);
          if (status.status === "scaned") {
            setStep("scanning");
          } else if (status.status === "confirmed") {
            setStep("done");
            const accounts = await api.get<AccountSummary[]>("/api/accounts");
            const newAcct = accounts.find((a) => a.status === "online");
            if (newAcct) {
              onSuccess(newAcct);
            }
            return;
          } else if (status.status === "expired") {
            setError("二维码已过期，请重新生成");
            setStep("idle");
            return;
          }
        } catch { /* polling errors are ok */ }
      }
    } catch (e) {
      setError(String(e));
      setStep("error");
    }
  };

  useEffect(() => {
    return () => pollingRef.current?.abort();
  }, []);

  return (
    <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
      <div className="bg-white dark:bg-gray-900 rounded-xl shadow-2xl p-6 w-96 max-w-[90vw]">
        <div className="flex justify-between items-center mb-4">
          <h2 className="text-lg font-bold">添加微信账号</h2>
          <button onClick={onClose} className="text-gray-400 hover:text-gray-600">✕</button>
        </div>

        {step === "idle" && (
          <div className="space-y-4">
            <input
              type="text"
              value={label}
              onChange={(e) => setLabel(e.target.value)}
              placeholder="账号名称 (可选)"
              className="w-full px-3 py-2 border rounded-lg text-sm"
            />
            <button
              onClick={startLogin}
              className="w-full py-2 bg-wechat-green text-white rounded-lg hover:bg-green-600"
            >
              开始扫码登录
            </button>
          </div>
        )}

        {(step === "loading" || step === "qrcode" || step === "scanning") && (
          <div className="text-center space-y-4">
            {qrUrl && (
              <div className="bg-white p-3 rounded-lg inline-block">
                <img
                  src={`https://api.qrserver.com/v1/create-qr-code/?size=200x200&data=${encodeURIComponent(qrUrl)}`}
                  alt="QR Code"
                  className="w-48 h-48"
                />
              </div>
            )}
            <p className="text-sm text-gray-500">
              {step === "scanning"
                ? "👀 已扫码，请在微信上确认登录..."
                : "📱 请用微信扫描二维码"}
            </p>
            <p className="text-xs text-gray-400">或打开链接: {qrUrl.slice(0, 50)}...</p>
          </div>
        )}

        {step === "done" && (
          <div className="text-center space-y-3">
            <p className="text-green-500 text-lg">✅ 登录成功!</p>
            <button
              onClick={onClose}
              className="px-4 py-2 bg-gray-200 rounded-lg text-sm hover:bg-gray-300"
            >
              关闭
            </button>
          </div>
        )}

        {error && (
          <div className="text-center space-y-3">
            <p className="text-red-500 text-sm">{error}</p>
            <button
              onClick={() => { setStep("idle"); setError(""); }}
              className="px-4 py-2 bg-gray-200 rounded-lg text-sm"
            >
              重试
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
