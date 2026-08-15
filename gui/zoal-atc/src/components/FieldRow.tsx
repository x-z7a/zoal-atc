import {useState, type FormEvent} from "react";

type Props = {
  label: string;
  placeholder?: string;
  submitLabel: string;
  inputMode?: "text" | "decimal" | "numeric";
  // Kept between submissions rather than cleared, for fields that show a
  // current setting rather than issue a one-off command.
  initialValue?: string;
  clearOnSubmit?: boolean;
  disabled?: boolean;
  // Masks the value, with a control to reveal it. For a credential: a token
  // should not sit legible on a cockpit screen by default, but a pilot who just
  // pasted one needs to be able to check it.
  secret?: boolean;
  onSubmit: (value: string) => void;
};

// A labelled input with the button that acts on it.
//
// The panel is a command source, never the decider: this collects a value and
// hands it over. Whether the value is allowed is the console's answer to give.
export function FieldRow({
  label,
  placeholder,
  submitLabel,
  inputMode = "text",
  initialValue = "",
  clearOnSubmit = true,
  disabled = false,
  secret = false,
  onSubmit,
}: Props) {
  const [value, setValue] = useState(initialValue);
  const [revealed, setRevealed] = useState(false);

  function submit(event: FormEvent): void {
    event.preventDefault();
    onSubmit(value.trim());
    if (clearOnSubmit) {
      setValue("");
    }
  }

  const inputId = `field-${label.toLowerCase().replace(/[^a-z0-9]+/g, "-")}`;

  return (
    <form className="control-row" onSubmit={submit}>
      <label className="label" htmlFor={inputId}>
        {label}
      </label>
      <input
        id={inputId}
        type={secret && !revealed ? "password" : "text"}
        inputMode={inputMode}
        placeholder={placeholder}
        autoComplete="off"
        value={value}
        disabled={disabled}
        onChange={(event) => setValue(event.target.value)}
      />
      {secret ? (
        <button
          type="button"
          aria-pressed={revealed}
          disabled={disabled}
          onClick={() => setRevealed((shown) => !shown)}
        >
          {revealed ? "Hide" : "Show"}
        </button>
      ) : null}
      <button type="submit" disabled={disabled}>
        {submitLabel}
      </button>
    </form>
  );
}
