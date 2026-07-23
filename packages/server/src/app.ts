import Fastify from "fastify";
import cors from "@fastify/cors";
import { initDb } from "./db/index.js";
import { accountRoutes } from "./routes/accounts.js";
import { contactRoutes } from "./routes/contacts.js";
import { messageRoutes } from "./routes/messages.js";
import { wcfRoutes } from "./routes/wcf-bridge.js";
import { wsRoutes } from "./ws/handler.js";
import { accountManager } from "./ilink/manager.js";

export async function createApp() {
  const app = Fastify({ logger: true });

  // CORS
  await app.register(cors, {
    origin: true,
    credentials: true,
  });

  // Init database (async for sql.js)
  await initDb();
  console.log("[Server] Database initialized.");

  // Register routes
  await accountRoutes(app);
  await contactRoutes(app);
  await messageRoutes(app);
  await wcfRoutes(app);
  await wsRoutes(app);

  // Boot account monitors
  await accountManager.boot();

  // Health check
  app.get("/api/health", async () => ({ ok: true, uptime: process.uptime() }));

  return app;
}
