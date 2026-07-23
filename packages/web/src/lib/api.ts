const BASE = ""; // proxied by Vite to :3028

export const api = {
  async get<T>(path: string): Promise<T> {
    const res = await fetch(`${BASE}${path}`);
    if (!res.ok) throw new Error(`GET ${path}: ${res.status}`);
    return res.json() as Promise<T>;
  },

  async post<T>(path: string, body?: unknown): Promise<T> {
    const hasBody = body !== undefined && body !== null;
    const res = await fetch(`${BASE}${path}`, {
      method: "POST",
      headers: hasBody ? { "Content-Type": "application/json" } : undefined,
      body: hasBody ? JSON.stringify(body) : undefined,
    });
    if (!res.ok) {
      const err = await res.text();
      throw new Error(`POST ${path}: ${res.status} ${err}`);
    }
    return res.json() as Promise<T>;
  },

  async patch<T>(path: string, body?: unknown): Promise<T> {
    const res = await fetch(`${BASE}${path}`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: body ? JSON.stringify(body) : undefined,
    });
    if (!res.ok) throw new Error(`PATCH ${path}: ${res.status}`);
    return res.json() as Promise<T>;
  },

  async del(path: string): Promise<void> {
    const res = await fetch(`${BASE}${path}`, { method: "DELETE" });
    if (!res.ok) throw new Error(`DELETE ${path}: ${res.status}`);
  },
};
