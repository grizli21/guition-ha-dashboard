# Contribuer

Les retours et idées sont les bienvenus, surtout sur le contrat MQTT et l'intégration Home Assistant.

1. Ouvrez d'abord une issue pour décrire le problème ou l'idée.
2. Forkez la branche `main` et faites une branche par sujet, une modification par commit.
3. Vérifiez localement avant la pull request :
   - `pio run -e ecran` doit compiler ;
   - `make -C simu` doit compiler et régénérer les captures (`simu/captures/`).
4. Si vous changez un sujet ou une clé MQTT, mettez d'abord `CONTRAT_MQTT.md` à jour : le document est la
   référence, le code et les automatisations HA le suivent.

Merci !
