import { createSignal } from "solid-js";
import eyeIcon from "../assets/icons/eye.svg";
import eyeSlashIcon from "../assets/icons/eye-slash.svg";

// A password input with a show/hide toggle pinned inside its right edge. The
// icon is an eye while the value is masked and an eye-with-slash while revealed.
//
// Visibility is uncontrolled by default (each field toggles itself). Pass
// `visible` and `onToggle` to control it externally, so several fields can share
// one reveal state and a Show click on any of them reveals them all.
export function PasswordField(props) {
  const [internalVisible, setInternalVisible] = createSignal(false);
  const controlled = () => props.visible !== undefined;
  const visible = () => (controlled() ? props.visible : internalVisible());
  const toggle = () =>
    controlled() ? props.onToggle?.() : setInternalVisible((shown) => !shown);

  return (
    <div class="password-field">
      <input
        id={props.id}
        name={props.name}
        type={visible() ? "text" : "password"}
        autocomplete={props.autocomplete}
        value={props.value}
        onInput={props.onInput}
        required={props.required}
      />
      <button
        type="button"
        class="password-toggle"
        onClick={toggle}
        aria-label={visible() ? "Hide password" : "Show password"}
        aria-pressed={visible()}
        title={visible() ? "Hide password" : "Show password"}
      >
        <img src={visible() ? eyeSlashIcon : eyeIcon} alt="" aria-hidden="true" />
      </button>
    </div>
  );
}
