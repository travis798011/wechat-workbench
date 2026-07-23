/**
 * 数据库初始化 — 使用 sql.js (纯 JS SQLite，无需 C++ 编译)
 */

import initSqlJs, { Database as SqlJsDatabase } from "sql.js";
import fs from "node:fs";
import path from "node:path";

let db: SqlJsDatabase | null = null;
const DB_PATH = path.join(process.cwd(), "data/workbench.db");

export async function initDb(): Promise<SqlJsDatabase> {
  if (db) return db;

  const dir = path.dirname(DB_PATH);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });

  const SQL = await initSqlJs();

  // Load existing or create new
  if (fs.existsSync(DB_PATH)) {
    const buffer = fs.readFileSync(DB_PATH);
    db = new SQL.Database(buffer);
  } else {
    db = new SQL.Database();
  }

  db.run("PRAGMA journal_mode = WAL");
  db.run("PRAGMA foreign_keys = ON");

  // Create tables
  db.run(`
    CREATE TABLE IF NOT EXISTS accounts (
      id TEXT PRIMARY KEY,
      label TEXT NOT NULL,
      bot_token TEXT NOT NULL DEFAULT '',
      base_url TEXT NOT NULL DEFAULT '',
      status TEXT NOT NULL DEFAULT 'offline',
      avatar TEXT,
      nickname TEXT,
      wechat_id TEXT,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    )
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS contacts (
      id TEXT PRIMARY KEY,
      account_id TEXT NOT NULL REFERENCES accounts(id),
      wechat_user_id TEXT NOT NULL,
      nickname TEXT,
      avatar TEXT,
      remark TEXT,
      last_message TEXT,
      last_msg_at INTEGER,
      unread_count INTEGER NOT NULL DEFAULT 0,
      is_muted INTEGER NOT NULL DEFAULT 0,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    )
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS messages (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      account_id TEXT NOT NULL,
      contact_id TEXT NOT NULL,
      wechat_user_id TEXT NOT NULL,
      direction TEXT NOT NULL,
      msg_type TEXT NOT NULL,
      content TEXT,
      media_url TEXT,
      media_type TEXT,
      wechat_msg_id TEXT,
      context_token TEXT,
      is_read INTEGER NOT NULL DEFAULT 0,
      created_at INTEGER NOT NULL
    )
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS sync_state (
      account_id TEXT PRIMARY KEY REFERENCES accounts(id),
      sync_buf TEXT NOT NULL DEFAULT '',
      updated_at INTEGER NOT NULL
    )
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS allow_from (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      account_id TEXT NOT NULL REFERENCES accounts(id),
      wechat_user_id TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      UNIQUE(account_id, wechat_user_id)
    )
  `);

  // Indexes
  db.run("CREATE INDEX IF NOT EXISTS idx_messages_contact ON messages(contact_id, created_at DESC)");
  db.run("CREATE INDEX IF NOT EXISTS idx_messages_account ON messages(account_id, created_at DESC)");
  db.run("CREATE INDEX IF NOT EXISTS idx_contacts_account ON contacts(account_id, last_msg_at DESC)");

  saveDb();
  console.log(`[DB] Initialized at ${DB_PATH}`);
  return db;
}

export function getDb(): SqlJsDatabase {
  if (!db) throw new Error("DB not initialized. Call initDb() first.");
  return db;
}

export function saveDb(): void {
  if (!db) return;
  const dir = path.dirname(DB_PATH);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  const data = db.export();
  const buffer = Buffer.from(data);
  fs.writeFileSync(DB_PATH, buffer);
}

export function closeDb(): void {
  if (db) {
    saveDb();
    db.close();
    db = null;
  }
}
