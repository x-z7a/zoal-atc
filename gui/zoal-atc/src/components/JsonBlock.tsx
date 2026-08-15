type Props = {
  value: unknown;
  empty?: string;
};

function render(value: unknown): string {
  try {
    return JSON.stringify(value, null, 2);
  } catch {
    return String(value);
  }
}

// A payload exactly as it arrived.
//
// This lives on the Debug tab and nowhere else. Serializing a whole session
// snapshot is not free, and the old panel did it on every flight_snapshot
// whether or not anyone had the disclosure open.
export function JsonBlock({value, empty = "Nothing yet."}: Props) {
  if (value === undefined || value === null) {
    return <pre className="json-block">{empty}</pre>;
  }
  return <pre className="json-block">{render(value)}</pre>;
}
