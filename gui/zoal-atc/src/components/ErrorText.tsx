type Props = {
  message: string;
};

// The console's own words, in front of the pilot.
//
// An action that silently does nothing is indistinguishable from one that
// worked, which on a radio panel is the worse of the two failures.
export function ErrorText({message}: Props) {
  if (message === "") {
    return null;
  }
  return (
    <p className="action-error" role="status">
      {message}
    </p>
  );
}
