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
  function onKeyDown(event: KeyboardEvent<HTMLDivElement>): void {
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
    <div className="tabs" role="tablist" aria-label="Panel sections" onKeyDown={onKeyDown}>
      {tabs.map((tab) => {
        const selected = tab.id === active;
        return (
          <button
            key={tab.id}
            type="button"
            role="tab"
            id={`tab-${tab.id}`}
            aria-selected={selected}
            aria-controls={`panel-${tab.id}`}
            tabIndex={selected ? 0 : -1}
            className={`tab ${selected ? "tab-active" : ""}`}
            onClick={() => onSelect(tab.id)}
          >
            {tab.label}
          </button>
        );
      })}
    </div>
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
// window drawing inside the sim's frame budget: the expensive views -- the raw
// session JSON, the bay table, the debug tail -- render nothing at all while
// the pilot is on Home.
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
