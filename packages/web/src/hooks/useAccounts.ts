import { useState, useEffect, useCallback } from "react";
import { api } from "../lib/api.ts";
import type { AccountSummary } from "@workbench/shared";

export function useAccounts() {
  const [accounts, setAccounts] = useState<AccountSummary[]>([]);

  const load = useCallback(async () => {
    try {
      const list = await api.get<AccountSummary[]>("/api/accounts");
      setAccounts(list);
    } catch (e) {
      console.error("Failed to load accounts:", e);
    }
  }, []);

  useEffect(() => {
    load();
    const interval = setInterval(load, 10_000); // poll every 10s
    return () => clearInterval(interval);
  }, [load]);

  const addAccount = useCallback(
    (acct: AccountSummary) => {
      setAccounts((prev) => {
        const idx = prev.findIndex((a) => a.id === acct.id);
        if (idx >= 0) {
          const next = [...prev];
          next[idx] = acct;
          return next;
        }
        return [...prev, acct];
      });
    },
    [],
  );

  const removeAccount = useCallback(async (id: string) => {
    try {
      await api.del(`/api/accounts/${id}`);
      setAccounts((prev) => prev.filter((a) => a.id !== id));
    } catch (e) {
      console.error("Failed to remove account:", e);
    }
  }, []);

  return { accounts, load, addAccount, removeAccount };
}
