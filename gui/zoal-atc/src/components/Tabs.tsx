import type {KeyboardEvent} from "react";

export type TabDefinition = {
  id: string;
  label: string;
};

type Props = {
  tabs: readonly TabDefinition[];
  active: string;
  onSelect: (id: string) => void;
};

// The panel's top-level navigation.
//
// Roles and arrow-key movement are not decoration: this window is used with a
// yoke in one hand, and a control that can only be reached by clicking exactly
// on it is harder to use than one that responds to a key.
export function Tabs({tabs, active, onSelect}: Props) {
  function onKeyDown(event: KeyboardEvent<HTMLUListElement>): void {
    const step = event.key === "ArrowRight" ? 1 : event.key === "ArrowLeft" ? -1 : 0;
    if (step === 0) {
      return;
    }
    event.preventDefault();
    const index = tabs.findIndex((tab) => tab.id === active);
    const next = tabs[(index + step + tabs.length) % tabs.length];
    if (next) {
      onSelect(next.id);
    }
  }

  return (
    <ul className="tabs" role="tablist" aria-label="Panel sections" onKeyDown={onKeyDown}>
      {tabs.map((tab) => {
        const selected = tab.id === active;
        return (
          <li key={tab.id} className="tab" role="presentation">
            <button
              type="button"
              role="tab"
              id={`tab-${tab.id}`}
              aria-selected={selected}
              aria-controls={`panel-${tab.id}`}
              tabIndex={selected ? 0 : -1}
              className={selected ? "active" : undefined}
              onClick={() => onSelect(tab.id)}
            >
              {tab.label}
            </button>
          </li>
        );
      })}
    </ul>
  );
}

type PanelProps = {
  id: string;
  active: string;
  children: React.ReactNode;
};

// Only the active panel is mounted.
//
// That is the answer to the objection phase 23 raised about React in a CEF
// window drawing inside the sim's frame budget: a tab the pilot is not looking
// at renders nothing at all.
export function TabPanel({id, active, children}: PanelProps) {
  if (id !== active) {
    return null;
  }
  return (
    <div role="tabpanel" id={`panel-${id}`} aria-labelledby={`tab-${id}`} className="tab-panel">
      {children}
    </div>
  );
}
