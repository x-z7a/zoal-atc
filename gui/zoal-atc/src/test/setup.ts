import "@testing-library/jest-dom/vitest";

// jsdom implements no layout, so it has no scrollIntoView. The radio log uses
// it to follow the tail. Stubbing it here rather than guarding the call keeps
// the production path the one that actually ships.
if (!Element.prototype.scrollIntoView) {
  Element.prototype.scrollIntoView = () => {};
}
