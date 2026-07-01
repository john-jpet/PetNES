/** IndexedDB-backed ROM library. Stores ROM data + metadata per hash. */

export interface LibraryEntry {
  hash:       string;
  name:       string;
  mapperId:   number;
  thumbnail:  string;   // base64 PNG data URL
  lastPlayed: number;   // timestamp
  size:       number;   // bytes
}

const DB_NAME    = "petnes-library";
const DB_VERSION = 1;
const META_STORE = "meta";
const DATA_STORE = "roms";

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      req.result.createObjectStore(META_STORE, { keyPath: "hash" });
      req.result.createObjectStore(DATA_STORE, { keyPath: "hash" });
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror   = () => reject(req.error);
  });
}

function tx<T>(
  db:    IDBDatabase,
  stores: string | string[],
  mode:  IDBTransactionMode,
  fn:    (tx: IDBTransaction) => IDBRequest<T>,
): Promise<T> {
  return new Promise((resolve, reject) => {
    const t   = db.transaction(stores, mode);
    const req = fn(t);
    req.onsuccess = () => resolve(req.result);
    req.onerror   = () => reject(req.error);
  });
}

export class RomLibrary {
  private db: IDBDatabase | null = null;

  async open(): Promise<void> {
    this.db = await openDb();
  }

  private get(db: IDBDatabase) { return db; }

  async save(
    hash:      string,
    name:      string,
    mapperId:  number,
    data:      Uint8Array,
    thumbnail: string,
  ): Promise<void> {
    const db = this.db!;
    const meta: LibraryEntry = {
      hash, name, mapperId, thumbnail,
      lastPlayed: Date.now(),
      size: data.length,
    };
    await Promise.all([
      tx(db, META_STORE, "readwrite", t => t.objectStore(META_STORE).put(meta)),
      tx(db, DATA_STORE, "readwrite", t => t.objectStore(DATA_STORE).put({ hash, data })),
    ]);
  }

  async updateLastPlayed(hash: string): Promise<void> {
    const db   = this.db!;
    const meta = await tx<LibraryEntry>(db, META_STORE, "readonly",
      t => t.objectStore(META_STORE).get(hash));
    if (meta) {
      meta.lastPlayed = Date.now();
      await tx(db, META_STORE, "readwrite", t => t.objectStore(META_STORE).put(meta));
    }
  }

  async getAll(): Promise<LibraryEntry[]> {
    const db = this.db!;
    return new Promise((resolve, reject) => {
      const req = db.transaction(META_STORE, "readonly")
        .objectStore(META_STORE).getAll();
      req.onsuccess = () =>
        resolve((req.result as LibraryEntry[]).sort((a, b) => b.lastPlayed - a.lastPlayed));
      req.onerror = () => reject(req.error);
    });
  }

  async getData(hash: string): Promise<Uint8Array | null> {
    const db  = this.db!;
    const row = await tx<{ hash: string; data: Uint8Array } | undefined>(
      db, DATA_STORE, "readonly", t => t.objectStore(DATA_STORE).get(hash));
    return row?.data ?? null;
  }

  async remove(hash: string): Promise<void> {
    const db = this.db!;
    await Promise.all([
      tx(db, META_STORE, "readwrite", t => t.objectStore(META_STORE).delete(hash)),
      tx(db, DATA_STORE, "readwrite", t => t.objectStore(DATA_STORE).delete(hash)),
    ]);
  }

  async has(hash: string): Promise<boolean> {
    const db  = this.db!;
    const key = await tx<IDBValidKey | undefined>(
      db, META_STORE, "readonly", t => t.objectStore(META_STORE).getKey(hash));
    return key !== undefined;
  }
}
