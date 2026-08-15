import type {Freshness} from "../domain/status";

type Props = {
  freshness: Freshness;
};

// Says which silence this is, or nothing at all.
//
// The old panel kept an always-present element and hid it with :empty. A
// component can simply not exist, which is one fewer way for an empty banner to
// take up space in a window that has little to spare.
export function StaleBanner({freshness}: Props) {
  if (freshness.message === "") {
    return null;
  }
  return (
    <p className="stale-banner is-stale" role="status">
      {freshness.message}
    </p>
  );
}
