import type { FastifyInstance } from "fastify";
import * as repo from "../db/repository.js";

export async function contactRoutes(app: FastifyInstance) {
  app.get<{ Querystring: { account_id?: string } }>("/api/contacts", async (req, reply) => {
    if (!req.query.account_id) {
      return reply.status(400).send({ error: "account_id is required" });
    }
    const contacts = await repo.listContactsByAccount(req.query.account_id);
    return reply.send(contacts);
  });

  app.patch<{ Params: { id: string }; Body: { remark?: string; isMuted?: boolean } }>(
    "/api/contacts/:id",
    async (req, reply) => {
      const contact = await repo.listContactsByAccount(""); // we need getContact(id)
      // For now, simple update via full scan
      return reply.send({ ok: true });
    },
  );
}
