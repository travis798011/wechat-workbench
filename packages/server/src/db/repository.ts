/**
 * 数据库操作层 — 基于 sql.js (原生 SQL)
 */

import { getDb, saveDb } from "./index.js";
import type { Account, Contact, Message } from "@workbench/shared";

// Helper: convert snake_case keys to camelCase
function toCamelCase(obj: Record<string, unknown>): Record<string, unknown> {
  const result: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(obj)) {
    const camelKey = key.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
    result[camelKey] = value;
  }
  return result;
}

// Helper: run query and return all rows as array
function queryAll<T = Record<string, unknown>>(sql: string, params: unknown[] = []): T[] {
  const stmt = getDb().prepare(sql);
  stmt.bind(params);
  const rows: T[] = [];
  while (stmt.step()) {
    rows.push(toCamelCase(stmt.getAsObject()) as unknown as T);
  }
  stmt.free();
  return rows;
}

function queryOne<T = Record<string, unknown>>(sql: string, params: unknown[] = []): T | undefined {
  const rows = queryAll<T>(sql, params);
  return rows.length > 0 ? rows[0] : undefined;
}

function run(sql: string, params: unknown[] = []): void {
  getDb().run(sql, params);
  saveDb();
}

// ---- Accounts ----

export async function listAccounts(): Promise<Account[]> {
  return queryAll<Account>("SELECT * FROM accounts ORDER BY created_at ASC");
}

export async function getAccount(id: string): Promise<Account | undefined> {
  return queryOne<Account>("SELECT * FROM accounts WHERE id = ?", [id]);
}

export async function upsertAccount(acct: Account): Promise<void> {
  const existing = queryOne<{ id: string }>("SELECT id FROM accounts WHERE id = ?", [acct.id]);
  if (existing) {
    run(`UPDATE accounts SET label=?,bot_token=?,base_url=?,status=?,avatar=?,nickname=?,wechat_id=?,updated_at=? WHERE id=?`, [
      acct.label, acct.botToken, acct.baseUrl, acct.status,
      acct.avatar, acct.nickname, acct.wechatId, Date.now(), acct.id,
    ]);
  } else {
    run(`INSERT INTO accounts(id,label,bot_token,base_url,status,avatar,nickname,wechat_id,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?)`, [
      acct.id, acct.label, acct.botToken, acct.baseUrl, acct.status,
      acct.avatar, acct.nickname, acct.wechatId, acct.createdAt, acct.updatedAt,
    ]);
  }
}

export async function updateAccountStatus(id: string, status: string): Promise<void> {
  run("UPDATE accounts SET status=?,updated_at=? WHERE id=?", [status, Date.now(), id]);
}

export async function deleteAccount(id: string): Promise<void> {
  run("DELETE FROM accounts WHERE id=?", [id]);
}

// ---- Contacts ----

export async function listContactsByAccount(accountId: string): Promise<Contact[]> {
  return queryAll<Contact>(
    "SELECT * FROM contacts WHERE account_id=? ORDER BY COALESCE(last_msg_at,0) DESC",
    [accountId],
  );
}

export async function upsertContact(c: Contact): Promise<void> {
  const existing = queryOne<{ id: string }>("SELECT id FROM contacts WHERE id=?", [c.id]);
  if (existing) {
    run(`UPDATE contacts SET account_id=?,wechat_user_id=?,nickname=?,avatar=?,remark=?,last_message=?,last_msg_at=?,unread_count=?,is_muted=?,updated_at=? WHERE id=?`, [
      c.accountId, c.wechatUserId, c.nickname, c.avatar, c.remark,
      c.lastMessage, c.lastMsgAt, c.unreadCount, c.isMuted ? 1 : 0,
      Date.now(), c.id,
    ]);
  } else {
    run(`INSERT INTO contacts(id,account_id,wechat_user_id,nickname,avatar,remark,last_message,last_msg_at,unread_count,is_muted,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)`, [
      c.id, c.accountId, c.wechatUserId, c.nickname, c.avatar, c.remark,
      c.lastMessage, c.lastMsgAt, c.unreadCount, c.isMuted ? 1 : 0,
      c.createdAt, c.updatedAt,
    ]);
  }
}

export async function updateContactUnread(contactId: string, increment: number): Promise<void> {
  const existing = queryOne<{ unread_count: number }>("SELECT unread_count FROM contacts WHERE id=?", [contactId]);
  if (existing) {
    const newCount = Math.max(0, (existing.unread_count ?? 0) + increment);
    run("UPDATE contacts SET unread_count=?,updated_at=? WHERE id=?", [newCount, Date.now(), contactId]);
  }
}

export async function markContactRead(contactId: string): Promise<void> {
  run("UPDATE contacts SET unread_count=0,updated_at=? WHERE id=?", [Date.now(), contactId]);
}

// ---- Messages ----

export async function listMessages(
  contactId: string,
  before?: number,
  limit = 50,
): Promise<Message[]> {
  if (before) {
    return queryAll<Message>(
      "SELECT * FROM messages WHERE contact_id=? AND id<? ORDER BY created_at DESC LIMIT ?",
      [contactId, before, limit],
    ).reverse();
  }
  return queryAll<Message>(
    "SELECT * FROM messages WHERE contact_id=? ORDER BY created_at DESC LIMIT ?",
    [contactId, limit],
  ).reverse();
}

export async function listMessagesByAccount(
  accountId: string,
  before?: number,
  limit = 100,
): Promise<Message[]> {
  if (before) {
    return queryAll<Message>(
      "SELECT * FROM messages WHERE account_id=? AND id<? ORDER BY created_at DESC LIMIT ?",
      [accountId, before, limit],
    ).reverse();
  }
  return queryAll<Message>(
    "SELECT * FROM messages WHERE account_id=? ORDER BY created_at DESC LIMIT ?",
    [accountId, limit],
  ).reverse();
}

export async function insertMessage(msg: Omit<Message, "id">): Promise<number> {
  run(`INSERT INTO messages(account_id,contact_id,wechat_user_id,direction,msg_type,content,media_url,media_type,wechat_msg_id,context_token,is_read,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)`, [
    msg.accountId, msg.contactId, msg.wechatUserId,
    msg.direction, msg.msgType, msg.content,
    msg.mediaUrl, msg.mediaType, msg.wechatMsgId, msg.contextToken,
    msg.isRead ? 1 : 0, msg.createdAt,
  ]);
  const row = queryOne<{ id: number }>("SELECT last_insert_rowid() as id");
  return row?.id ?? 0;
}

export async function markMessagesRead(contactId: string): Promise<void> {
  run("UPDATE messages SET is_read=1 WHERE contact_id=? AND is_read=0", [contactId]);
}

// ---- Sync State ----

export async function getSyncBuf(accountId: string): Promise<string> {
  const row = queryOne<{ sync_buf: string }>("SELECT sync_buf FROM sync_state WHERE account_id=?", [accountId]);
  return row?.sync_buf ?? "";
}

export async function updateSyncBuf(accountId: string, buf: string): Promise<void> {
  const existing = queryOne<{ account_id: string }>("SELECT account_id FROM sync_state WHERE account_id=?", [accountId]);
  if (existing) {
    run("UPDATE sync_state SET sync_buf=?,updated_at=? WHERE account_id=?", [buf, Date.now(), accountId]);
  } else {
    run("INSERT INTO sync_state(account_id,sync_buf,updated_at) VALUES(?,?,?)", [accountId, buf, Date.now()]);
  }
}

// ---- Allow-From ----

export async function getAllowFrom(accountId: string): Promise<string[]> {
  const rows = queryAll<{ wechat_user_id: string }>(
    "SELECT wechat_user_id FROM allow_from WHERE account_id=?", [accountId],
  );
  return rows.map((r) => r.wechat_user_id);
}

export async function addAllowFrom(accountId: string, wechatUserId: string): Promise<boolean> {
  try {
    run("INSERT OR IGNORE INTO allow_from(account_id,wechat_user_id,created_at) VALUES(?,?,?)", [
      accountId, wechatUserId, Date.now(),
    ]);
    return true;
  } catch {
    return false;
  }
}

export async function removeAllowFrom(accountId: string, wechatUserId: string): Promise<void> {
  run("DELETE FROM allow_from WHERE account_id=? AND wechat_user_id=?", [accountId, wechatUserId]);
}

// ---- Search ----

export async function searchContacts(q: string): Promise<Contact[]> {
  return queryAll<Contact>(
    `SELECT * FROM contacts WHERE nickname LIKE ? OR wechat_user_id LIKE ? OR remark LIKE ? ORDER BY COALESCE(last_msg_at,0) DESC LIMIT 20`,
    [`%${q}%`, `%${q}%`, `%${q}%`],
  );
}
