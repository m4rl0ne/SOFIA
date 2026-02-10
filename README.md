# SOFIA
**S**ecure **O**verlay-Network **F**or **I**ndustrial **A**pplications

## How to use
1) docker compose up --build
2) See logs in console: node-1, node-2, node-3, node-4 are joining the ring. Wait a few seconds for the ring to settle 
3) Start or stop nodes to see the ring healing: docker compose stop node-2 or docker compose start node-2
