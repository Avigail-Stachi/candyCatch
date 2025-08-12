import React, { useState, useEffect, useCallback } from "react";
import "../styles/FirebaseConnect.css";
import TurnModal from "./TurnModal.jsx";
import { ref, runTransaction, onValue, get, set } from "../firebaseConfig.js";

function FirebaseConnect({ setMyAssignedTurn, message, setMessage, db, userId }) {
  const [currentServingNumber, setCurrentServingNumber] = useState(0);
  const [lastIssuedNumber, setLastIssuedNumber] = useState(0);
  const [isFirebaseConnected, setIsFirebaseConnected] = useState(false);
  const [showTurnModal, setShowTurnModal] = useState(false);
  const [modalTurnNumber, setModalTurnNumber] = useState(null);
  const [isQueueEmpty, setIsQueueEmpty] = useState(true);
  const [myAssignedTurn, setMyAssignedTurnLocal] = useState(null);
  const [buttonDisabled, setButtonDisabled] = useState(false);

  // עדכון מצב התור
  useEffect(() => {
    setIsQueueEmpty(currentServingNumber === 0 || currentServingNumber > lastIssuedNumber);
  }, [currentServingNumber, lastIssuedNumber]);

  // חיבור לפיירבייס ועדכון ערכים בזמן אמת
  useEffect(() => {
    if (!db || !userId) {
      setIsFirebaseConnected(false);
      return;
    }

    setIsFirebaseConnected(true);

    const servingRef = ref(db, "queue/currentServingNumber");
    const lastIssuedRef = ref(db, "queue/lastIssuedNumber");

    const unsubscribeServing = onValue(servingRef, (snapshot) => {
      const number = snapshot.val();
      setCurrentServingNumber(number !== null ? number : 0);
    });

    const unsubscribeLastIssued = onValue(lastIssuedRef, (snapshot) => {
      const number = snapshot.val();
      setLastIssuedNumber(number !== null ? number : 0);
    });

    return () => {
      unsubscribeServing();
      unsubscribeLastIssued();
    };
  }, [db, userId]);

  // בקשת תור חדש
  const requestNewTurn = useCallback(async () => {
    if (!db || !userId) {
      setMessage("Database not connected or user not authenticated");
      return;
    }

    setButtonDisabled(true);
    setMessage("Requesting your turn...");

    const issuedRef = ref(db, "queue/lastIssuedNumber");
    const servingRef = ref(db, "queue/currentServingNumber");

    try {
      const [servingSnap, issuedSnap] = await Promise.all([
        get(servingRef),
        get(issuedRef),
      ]);

      const currentServing = servingSnap.val() || 0;
      const lastIssued = issuedSnap.val() || 0;

      const newTurnData = await runTransaction(issuedRef, (currentData) => {
        return (currentData || 0) + 1;
      });

      if (!newTurnData.committed) {
        setMessage("Failed to get a turn. Please try again.");
        setButtonDisabled(false);
        return;
      }

      const assignedTurn = newTurnData.snapshot.val();

      // אם התור ריק – נתחיל מהתור החדש
      if (currentServing === 0) {
        await set(servingRef, assignedTurn);
        setIsQueueEmpty(false); // לעדכן ידנית כי אין שינוי ב-onValue מיד
      }

      setMyAssignedTurnLocal(assignedTurn);
      if (setMyAssignedTurn) setMyAssignedTurn(assignedTurn);

      setModalTurnNumber(assignedTurn);
      setShowTurnModal(true);
      setMessage(`Your turn is: ${assignedTurn}`);
    } catch (error) {
      console.error("Error requesting turn:", error);
      setMessage("Error requesting turn. Please try again.");
      setButtonDisabled(false);
    }
  }, [db, userId, setMyAssignedTurn, setMessage]);

  // סגירת מודאל
  const handleCloseModal = () => {
    setShowTurnModal(false);
    setModalTurnNumber(null);
    setButtonDisabled(false);
  };

  // מעקב אחרי מצב התור האישי
  useEffect(() => {
    if (!myAssignedTurn) return;

    if (currentServingNumber === myAssignedTurn) {
      setMessage(`Your turn (${myAssignedTurn}) is now being served!`);
    } else if (currentServingNumber > myAssignedTurn) {
      setMessage(`Your turn (${myAssignedTurn}) is over.`);
    } else {
      setMessage(`Your turn is: ${myAssignedTurn}`);
    }
  }, [currentServingNumber, myAssignedTurn, setMessage]);

  return (
    <div className="firebase-container">
      <p className={`firebase-status ${isFirebaseConnected ? "connected" : "disconnected"}`}>
        Firebase Status: {isFirebaseConnected ? "Connected" : "Disconnected"}
      </p>

      {isQueueEmpty ? (
        <h1>The queue is currently empty</h1>
      ) : (
        <h1>Current Serving Turn: {currentServingNumber}</h1>
      )}

      <h2>Next Available Turn: {lastIssuedNumber + 1}</h2>

      <button
        onClick={requestNewTurn}
        className="btn-request-turn"
        disabled={buttonDisabled || showTurnModal}
      >
        Get New Turn
      </button>

      {message && myAssignedTurn !== null && (
        <p className="message">{message}</p>
      )}

      {showTurnModal && (
        <TurnModal
          turnNumber={modalTurnNumber}
          onClose={handleCloseModal}
          isImmediate={isQueueEmpty}
        />
      )}
    </div>
  );
}

export default FirebaseConnect;
