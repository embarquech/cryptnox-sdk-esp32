#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* =========================
 * WiFi Configuration
 * ========================= */
#define WIFI_SSID        "<YOUR_SSID>"
#define WIFI_PASSWORD    "<YOUR_PASSWORD>"

/* =========================
 * Ethereum / RPC
 * ========================= */
/**
 * Choose ONE provider below and comment out the other.
 *
 * Option A — PublicNode (free, no account required)
 *   Uncomment the three lines under "Option A".
 *
 * Option B — Infura (requires a free account at app.infura.io)
 *   1. Create an API key in the Infura dashboard.
 *   2. In the key's Settings tab, reveal (or generate) the API Secret.
 *   3. Uncomment the four lines under "Option B" and fill in the values.
 *   Note: the API Secret must have NO leading or trailing spaces.
 */

/* --- Option A: PublicNode ----------------------------------------- */
#define RPC_HOST       "ethereum-sepolia-rpc.publicnode.com"
#define RPC_PORT       443
#define RPC_URL        "https://" RPC_HOST
/* No authentication needed — leave RPC_PROJECT_ID / RPC_API_SECRET
 * undefined (or comment them out) when using PublicNode.            */

/* --- Option B: Infura --------------------------------------------- */
/* #define RPC_HOST        "sepolia.infura.io"                        */
/* #define RPC_PORT        443                                         */
/* #define RPC_PROJECT_ID  "<YOUR_INFURA_PROJECT_ID>"                 */
/* #define RPC_URL         "https://sepolia.infura.io/v3/" RPC_PROJECT_ID */
/* #define RPC_API_SECRET  "<YOUR_INFURA_API_SECRET>"                 */

/* =========================
 * Wallet / Keys (SENSITIVE)
 * ========================= */
/**
 * ⚠️ NEVER COMMIT config.h — it contains credentials.
 *    Add config.h to .gitignore.
 */

#define CARD_PIN      "<CARD_PIN>"   /* 4-9 digit PIN, e.g. "000000000" */
#define CARD_PIN_LEN  (9U)           /* number of digits in CARD_PIN     */

/* =========================
 * Ethereum Addresses
 * ========================= */
/* Sender address — lowercase hex, no 0x prefix */
#define ADDR_FROM         "<SENDER_ADDRESS>"

/* Recipient address */
#define ADDR_TO           "<RECIPIENT_ADDRESS>"

/* USDC contract address (Sepolia testnet) */
#define ADDR_USDC         "<USDC_CONTRACT_ADDRESS>"

/* =========================
 * Transaction Parameters
 * ========================= */
#define CHAIN_ID_SEPOLIA  11155111

/* Amount in token smallest unit (USDC has 6 decimals) */
#define AMOUNT_USDC       1000000UL   /* 1.0 USDC */

/* Gas parameters (in wei) */
#define MAX_PRIORITY_FEE  2000000000ULL  /* 2 Gwei */
#define MAX_FEE           4000000000ULL  /* 4 Gwei */
#define GAS_LIMIT_ERC20   60000ULL

#endif /* CONFIG_H */
