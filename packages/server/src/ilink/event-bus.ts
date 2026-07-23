/** Event emitter for in-process event routing */
type Listener = (...args: any[]) => void;

class EventBus {
  private listeners = new Map<string, Set<Listener>>();

  on(event: string, fn: Listener): void {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event)!.add(fn);
  }

  off(event: string, fn: Listener): void {
    this.listeners.get(event)?.delete(fn);
  }

  emit(event: string, ...args: unknown[]): void {
    for (const fn of this.listeners.get(event) ?? []) {
      try { fn(...args); } catch (e) { console.error(`[EventBus] ${event} handler error:`, e); }
    }
  }
}

export const bus = new EventBus();
