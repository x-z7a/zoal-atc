import type {DebugRecordView} from "../bridge/views";

// The categories the console records under (console/internal/debuglog).
// Telemetry is deliberately absent: it goes to its own file and would bury
// everything else here.
export const DEBUG_CATEGORIES: ReadonlyArray<{id: string; label: string}> = [
  {id: "", label: "All"},
  {id: "pilot_input", label: "Heard"},
  {id: "state_machine", label: "Engine"},
  {id: "ai_call", label: "Model"},
  {id: "delivery", label: "Delivery"},
  {id: "initiative", label: "Initiative"},
  {id: "guidance", label: "Guidance"},
  {id: "latency", label: "Latency"},
  {id: "connection", label: "Connection"},
  {id: "lifecycle", label: "Lifecycle"},
];

type Props = {
  records: readonly DebugRecordView[];
  category: string;
  loading: boolean;
  onCategory: (category: string) => void;
  onRefresh: () => void;
};

function summarize(fields: Record<string, unknown> | undefined): string {
  if (!fields) {
    return "";
  }
  // event first when there is one: it is the field that says what happened,
  // and a record whose first line is a hash is unreadable at a glance.
  const keys = Object.keys(fields).sort((a, b) =>
    a === "event" ? -1 : b === "event" ? 1 : a.localeCompare(b),
  );
  return keys
    .map((key) => {
      const value = fields[key];
      const text = typeof value === "object" ? JSON.stringify(value) : String(value);
      return `${key}=${text.length > 120 ? `${text.slice(0, 119)}…` : text}`;
    })
    .join("  ");
}

// The console's structured debug log, in the window.
//
// The records are already written, already tested and already carry the model
// prompts and responses; until now the only way to read them was to open
// debug.jsonl on the machine running the console.
export function DebugTail({records, category, loading, onCategory, onRefresh}: Props) {
  return (
    <>
      <div className="chips">
        {DEBUG_CATEGORIES.map((option) => (
          <button
            key={option.id || "all"}
            type="button"
            className={`chip ${option.id === category ? "chip-active" : ""}`}
            aria-pressed={option.id === category}
            onClick={() => onCategory(option.id)}
          >
            {option.label}
          </button>
        ))}
        <button type="button" className="chip" onClick={onRefresh} disabled={loading}>
          {loading ? "Reading…" : "Refresh"}
        </button>
      </div>

      {records.length === 0 ? (
        <p className="bay-empty">
          {loading ? "Reading the log…" : "No records under this category yet."}
        </p>
      ) : (
        <ol className="debug-tail">
          {/* Newest first: the thing you are debugging just happened. */}
          {[...records].reverse().map((record, index) => (
            <li key={`${record.ts ?? ""}-${index}`}>
              <span className="debug-meta">
                {record.ts ?? ""} {record.category ?? ""}
              </span>
              <span className="debug-fields">{summarize(record.fields)}</span>
            </li>
          ))}
        </ol>
      )}
    </>
  );
}
