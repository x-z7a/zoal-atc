import {useState, type FormEvent} from "react";

type Props = {
  disabled?: boolean;
  onSend: (text: string) => void;
  onTune: (frequency: string) => void;
};

// The bottom of the conversation, where the things you do to the radio live.
//
// Docked to the log rather than floating under it: a chat has its input at the
// bottom of the chat, and on a 620px window every detached row is space the
// conversation does not get.
export function Composer({disabled = false, onSend, onTune}: Props) {
  const [text, setText] = useState("");
  const [frequency, setFrequency] = useState("");

  function send(event: FormEvent): void {
    event.preventDefault();
    const trimmed = text.trim();
    if (trimmed === "") {
      return;
    }
    onSend(trimmed);
    setText("");
  }

  function tune(event: FormEvent): void {
    event.preventDefault();
    const trimmed = frequency.trim();
    if (trimmed === "") {
      return;
    }
    onTune(trimmed);
    setFrequency("");
  }

  return (
    <div className="composer">
      {/* Tuning is a radio action, so it belongs with the radio rather than in
          a controls block of its own. Narrow, because a frequency is six
          characters and the transmission is a sentence. */}
      <form className="composer-tune" onSubmit={tune}>
        <input
          type="text"
          inputMode="decimal"
          aria-label="Tune COM1"
          placeholder="118.800"
          autoComplete="off"
          value={frequency}
          disabled={disabled}
          onChange={(event) => setFrequency(event.target.value)}
        />
        <button type="submit" disabled={disabled}>
          Tune
        </button>
      </form>

      <form className="composer-say" onSubmit={send}>
        <input
          type="text"
          aria-label="Transmit"
          placeholder="Type a transmission…"
          autoComplete="off"
          value={text}
          disabled={disabled}
          onChange={(event) => setText(event.target.value)}
        />
        <button type="submit" disabled={disabled}>
          Send
        </button>
      </form>
    </div>
  );
}
