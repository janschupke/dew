import '@testing-library/jest-dom/vitest';

/*  jsdom knows what a <dialog> IS and not what it does.

    As of jsdom 29 HTMLDialogElement exists, reflects `open`, and has neither
    showModal nor close - so a component that opens one throws
    "showModal is not a function" and nothing on this side can see the viewer
    work at all.

    So the two calls are supplied here, and what they supply is exactly the
    `open` flag. What they cannot supply is the half the component was chosen
    FOR: the top layer, the focus trap, Escape, and the rest of the page marked
    inert are the browser's, and no test in this directory covers them. What the
    shot test covers is the wiring - the button opens it, the close control and
    a click on the backdrop close it - and `tests/shot.test.tsx` asserts this
    shim is in place first, so it cannot pass by finding a dialog that never
    moved.
*/
// Read through a Partial, because the TYPES say both calls are there and only
// this runtime says otherwise - a plain `=== undefined` is a comparison the
// checker can prove is always false, and it says so.
const dialogs = HTMLDialogElement.prototype as Partial<HTMLDialogElement>;

if (dialogs.showModal === undefined) {
  dialogs.showModal = function showModal(this: HTMLDialogElement) {
    this.open = true;
  };

  dialogs.close = function close(this: HTMLDialogElement) {
    this.open = false;
    this.dispatchEvent(new Event('close'));
  };
}
