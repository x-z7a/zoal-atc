type Props = {
  label: string;
  value: string;
  detail?: string;
  // An action this readout can take, when there is one obvious thing to do
  // with it — importing the plan the "filed plan" tile is reporting on.
  action?: {label: string; onClick: () => void; disabled?: boolean};
};

// One number the pilot glances at, with the word for what it is. The detail
// line is for the qualifier that would be meaningless on its own -- a standby
// frequency under the active one, a groundspeed under the altitude.
//
// Deliberately small. Six of these sit above the radio in a 620px window, so
// every pixel they take is a line of conversation nobody can see.
export function Tile({label, value, detail, action}: Props) {
  return (
    <article className="tile">
      <span className="label">
        {label}
        {action ? (
          <button
            type="button"
            className="tile-action"
            onClick={action.onClick}
            disabled={action.disabled}
          >
            {action.label}
          </button>
        ) : null}
      </span>
      <strong>{value}</strong>
      {detail ? <span className="tile-detail">{detail}</span> : null}
    </article>
  );
}
