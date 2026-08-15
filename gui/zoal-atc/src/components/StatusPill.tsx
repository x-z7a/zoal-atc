import type {ReactNode} from "react";

type Props = {
  ready: boolean;
  children: ReactNode;
};

// A one-word verdict on something the pilot cannot check for themselves. Ready
// and waiting are colour-coded because the panel is read at a glance, but the
// text says which it is too -- colour alone is not a status.
export function StatusPill({ready, children}: Props) {
  return (
    <span className={`status ${ready ? "status-ready" : "status-waiting"}`}>{children}</span>
  );
}
