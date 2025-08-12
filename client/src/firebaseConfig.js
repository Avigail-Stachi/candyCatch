// ייבוא פונקציות נדרשות מ-Firebase SDK
import { initializeApp } from "firebase/app";
import {
  getDatabase,
  ref,
  set,
  onValue,
  runTransaction,
  get,
} from "firebase/database";
import {
  getAuth,
  signInAnonymously,
  signInWithCustomToken,
  onAuthStateChanged,
} from "firebase/auth"; // ייבוא עבור אימות

// הגדרות Firebase - יש להחליף בפרטים שלך
const firebaseConfig = {
  apiKey: process.env.REACT_APP_FIREBASE_API_KEY,
  authDomain: process.env.REACT_APP_FIREBASE_AUTH_DOMAIN,
  databaseURL: process.env.REACT_APP_FIREBASE_DATABASE_URL,
  projectId: process.env.REACT_APP_FIREBASE_PROJECT_ID,
  storageBucket: process.env.REACT_APP_FIREBASE_STORAGE_BUCKET,
  messagingSenderId: process.env.REACT_APP_FIREBASE_MESSAGING_SENDER_ID,
  appId: process.env.REACT_APP_FIREBASE_APP_ID,
  measurementId: process.env.REACT_APP_FIREBASE_MEASUREMENT_ID,
};

// אתחול Firebase
const app = initializeApp(firebaseConfig);
const database = getDatabase(app);
const auth = getAuth(app); // אתחול שירות האימות

// ייצוא האובייקטים והפונקציות לשימוש ברחבי האפליקציה
export {
  database,
  ref,
  set,
  onValue,
  runTransaction,
  get,
  auth,
  signInAnonymously,
  signInWithCustomToken,
  onAuthStateChanged,
};
