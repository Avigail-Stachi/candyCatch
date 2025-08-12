import React from "react";
import "../styles/TurnModal.css";

function TurnModal({ turnNumber, onClose }) {
  if (!turnNumber) return null;

  return (
    <div className="modal-overlay">
      <div className="modal-content">
        <button className="modal-close-button" onClick={onClose}>
          &times;
        </button>
        <h2>Your turn is:</h2>
        <p className="turn-number">{turnNumber}</p>
      </div>
    </div>
  );
}

export default TurnModal;
