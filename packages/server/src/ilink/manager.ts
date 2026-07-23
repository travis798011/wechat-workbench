/**
 * 多账号 Monitor 管理器
 *
 * 管理 accountId → AccountMonitor 的映射，负责启动/停止/重启。
 */

import { AccountMonitor } from "./monitor.js";
import * as repo from "../db/repository.js";
import { bus } from "./event-bus.js";

class AccountManager {
  private monitors = new Map<string, AccountMonitor>();

  /** Boot: start monitors for all accounts with status "online" */
  async boot(): Promise<void> {
    const accounts = await repo.listAccounts();
    for (const acct of accounts) {
      if (acct.status === "online") {
        console.log(`[Manager] Resuming monitor for ${acct.id} (${acct.label})`);
        this.startMonitor(acct.id, acct.botToken);
      }
    }
    console.log(`[Manager] Booted ${this.monitors.size} monitors.`);
  }

  /** Start a single monitor */
  startMonitor(accountId: string, token: string): void {
    // Stop existing if any
    this.stopMonitor(accountId);

    const monitor = new AccountMonitor(accountId, token);
    this.monitors.set(accountId, monitor);
    monitor.start().catch((e) => {
      console.error(`[Manager] Monitor ${accountId} failed: ${e}`);
    });
  }

  /** Stop a single monitor */
  stopMonitor(accountId: string): void {
    const monitor = this.monitors.get(accountId);
    if (monitor) {
      monitor.stop().catch(console.error);
      this.monitors.delete(accountId);
    }
  }

  /** Stop all monitors */
  async shutdown(): Promise<void> {
    const stops = Array.from(this.monitors.values()).map((m) => m.stop().catch(console.error));
    await Promise.all(stops);
    this.monitors.clear();
    console.log("[Manager] All monitors stopped.");
  }

  /** Check if a monitor is running */
  isRunning(accountId: string): boolean {
    const m = this.monitors.get(accountId);
    return m != null && m.status === "running";
  }

  /** Update bot token (e.g. after re-login) */
  updateToken(accountId: string, token: string): void {
    const m = this.monitors.get(accountId);
    if (m) {
      m.updateToken(token);
    }
  }

  getStatuses(): Map<string, string> {
    const map = new Map<string, string>();
    for (const [id, m] of this.monitors) {
      map.set(id, m.status);
    }
    return map;
  }
}

export const accountManager = new AccountManager();
