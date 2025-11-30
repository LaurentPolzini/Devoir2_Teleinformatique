# Utilisation du code :
    - Création de l'exécutable avec "make"
    - Lancement de l'émulation avec "./emulation"
    - Nettoyage des fichiers objets avec "make clean"

Il n'y a pas de code correction d'erreur dans cette simulation. Ainsi, même 1% est énorme, chaque trame sera refusée car CRC-16 incorrect.

Ainsi, pour observer la fiabilité du protocole, il vaut mieux faire varier proba de perte.


# Pour aller plus loin
    Pour aller plus loin, nous avons créé une socket et l'avons connecté sur des ports locaux. Cela n'est pas actuellement fonctionnel, mais la simulation reste assez proche de la réalité.

    De base, par les limitations de nos connaissances, nous avions du code Python et du code C. Le code C appelant certains fonctions utilitaires en Python. Les conversions de types étaient difficilement gérable, alors le code Python a été ré-écrit en C.