import React, { useState, useEffect } from "react";
import "./styles/App.css";

// ייבוא כל הפונקציות והאובייקטים מ-firebaseConfig.js
import {
  database,
  auth,
  signInAnonymously,
  onAuthStateChanged,
} from "./firebaseConfig.js";
import FirebaseConnect from "./componnents/FirebaseConnect";

function App() {
  const [myAssignedTurn, setMyAssignedTurn] = useState(null);
  const [message, setMessage] = useState("Welcome! Get your turn.");
  const [userId, setUserId] = useState(null); // מזהה המשתמש המאומת
  const [isFirebaseReady, setIsFirebaseReady] = useState(false); // דגל לאינדיקציה ש-Firebase מוכן

  useEffect(() => {
    // וודא שאובייקט האימות קיים
    if (!auth) {
      console.error("Firebase Auth object is not initialized.");
      setMessage(
        "Error: Firebase Auth is not connected. Check console for details."
      );
      return;
    }

    // האזנה לשינויים במצב האימות
    const unsubscribeAuth = onAuthStateChanged(auth, async (user) => {
      if (user) {
        // משתמש מחובר
        setUserId(user.uid);
        setIsFirebaseReady(true);
        console.log(
          "App.jsx: Firebase Auth State Changed: User is signed in with UID:",
          user.uid
        );
      } else {
        // משתמש לא מחובר, נסה להתחבר אנונימית
        setUserId(null);
        console.log(
          "App.jsx: Firebase Auth State Changed: User is signed out. Attempting anonymous sign-in."
        );
        try {
          await signInAnonymously(auth);
          console.log("App.jsx: Signed in anonymously.");
          // ה-onAuthStateChanged יופעל שוב עם המשתמש האנונימי החדש
        } catch (error) {
          console.error("App.jsx: Firebase Anonymous Sign-in Error:", error);
          setMessage(
            "Error: Failed to sign in anonymously to Firebase. Check console."
          );
          setIsFirebaseReady(false);
        }
      }
    });

    // פונקציית ניקוי עבור ה-useEffect
    return () => {
      unsubscribeAuth();
    };
  }, []); // ריצה פעם אחת בלבד בעת טעינת הקומפוננטה

  return (
    <div className="App">
      <img src="/logo.png" alt="CandyCatch" className="app-logo"></img>
      {isFirebaseReady ? ( // רנדר את FirebaseConnect רק כאשר Firebase מוכן ומאומת
        <FirebaseConnect
          myAssignedTurn={myAssignedTurn}
          setMyAssignedTurn={setMyAssignedTurn}
          message={message}
          setMessage={setMessage}
          db={database} // העבר את אובייקט ה-database
          userId={userId} // העבר את ה-userId
        />
      ) : (
        <p className="message error">Initializing Firebase...</p> // הודעת טעינה
      )}
    </div>
  );
}

export default App;
