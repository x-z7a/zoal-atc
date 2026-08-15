type Props = {
  label: string;
  checked: boolean;
  disabled?: boolean;
  onChange: (checked: boolean) => void;
};

// A setting that is on or off.
//
// A real checkbox rather than a styled div: it is reachable by keyboard, it
// announces its own state, and the label is attached to it — this window is
// used with a yoke in one hand.
export function Toggle({label, checked, disabled = false, onChange}: Props) {
  const id = `toggle-${label.toLowerCase().replace(/[^a-z0-9]+/g, "-")}`;
  return (
    <div className="toggle-row">
      <input
        id={id}
        type="checkbox"
        checked={checked}
        disabled={disabled}
        onChange={(event) => onChange(event.target.checked)}
      />
      <label htmlFor={id}>{label}</label>
    </div>
  );
}
