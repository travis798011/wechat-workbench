import { createApp } from "./app.js";

const PORT = parseInt(process.env.PORT ?? "3028", 10);
const HOST = process.env.HOST ?? "0.0.0.0";

async function main() {
  const app = await createApp();

  // Graceful shutdown
  const shutdown = async () => {
    console.log("\n[Server] Shutting down...");
    const { accountManager } = await import("./ilink/manager.js");
    await accountManager.shutdown();
    await app.close();
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);

  await app.listen({ port: PORT, host: HOST });
  console.log(`[Server] Listening on http://${HOST}:${PORT}`);
}

main().catch((e) => {
  console.error("Fatal:", e);
  process.exit(1);
});
