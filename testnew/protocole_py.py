import socket
import time
import random
import sys
from pathlib import Path

# On réutilise tout le travail de crc.py
from crcv2 import Frame, encode_frame_to_bytes, decode_frame_from_bytes

# ==========================
# Constantes du protocole
# ==========================

LOCALHOST = "127.0.0.1"

EMITTER_PORT  = 30000
RECEIVER_PORT = 20000

# Types de trame (commande)
CMD_DATA      = 0
CMD_ACK       = 1
CMD_CON_CLOSE = 5  # fin de communication

# Paramètres Go-Back-N
# Ici on simplifie : num_seq est un index de trame sur 1 octet (0..255),
# donc on peut envoyer jusqu'à 255 trames sans wrap-around.
N_SEQ        = 256
WINDOW_SIZE  = 10      # taille de fenêtre raisonnable (< N_SEQ)
DATA_MAX_LEN = 100     # max 100 octets par trame, comme dans le TP


# ========================================
# Canal non fiable (erreurs / pertes / délai)
# ========================================

def introduce_byte_error(b: int, prob_error: float) -> int:
    """
    Flippe des bits dans un octet selon prob_error (probabilité par bit entre 0.0 et 1.0).
    """
    for bit in range(8):
        if random.random() < prob_error:
            b ^= (1 << bit)
    return b


def send_through_channel(sock: socket.socket,
                         data: bytes,
                         dest_addr,
                         prob_error: float,
                         prob_loss: float,
                         delay_max_ms: int) -> bool:
    """
    Applique pertes, erreurs et délais puis envoie sur le socket.

    prob_error : proba par bit (ex: 0.01 = 1% par bit)
    prob_loss  : proba de perdre complètement la trame (0.0..1.0)
    delay_max_ms : délai max en millisecondes
    """
    # Perte
    if random.random() < prob_loss:
        # trame perdue, rien envoyé
        return False

    # Erreurs de bits
    noisy = bytearray()
    for b in data:
        noisy.append(introduce_byte_error(b, prob_error))

    # Délai
    if delay_max_ms > 0:
        delay = random.randint(0, delay_max_ms)
        time.sleep(delay / 1000.0)

    sock.sendto(bytes(noisy), dest_addr)
    return True


# ==========================
# Utilitaires
# ==========================

def split_file_into_frames(path: str) -> list[Frame]:
    """
    Lit un fichier binaire et le découpe en trames DATA de DATA_MAX_LEN octets max.
    num_seq = index de trame (0,1,2,...) tant qu'on ne dépasse pas 255.
    """
    data = Path(path).read_bytes()
    frames: list[Frame] = []
    seq = 0

    for i in range(0, len(data), DATA_MAX_LEN):
        chunk = data[i:i + DATA_MAX_LEN]
        if seq >= 256:
            raise ValueError("Trop de trames pour un num_seq sur 1 octet (>255)")
        frames.append(Frame(commande=CMD_DATA, num_seq=seq, info=chunk))
        seq += 1

    return frames


def send_ack(sock: socket.socket,
             dest_addr,
             last_good_seq: int,
             prob_error: float,
             prob_loss: float,
             delay_max_ms: int) -> None:
    """
    Envoie une trame ACK avec num_seq = last_good_seq (ou 255 si aucun).
    """
    if last_good_seq < 0:
        seq_field = 255  # "aucune trame encore validée"
    else:
        seq_field = last_good_seq & 0xFF

    ack_frame = Frame(commande=CMD_ACK, num_seq=seq_field, info=b"")
    raw = encode_frame_to_bytes(ack_frame)
    send_through_channel(sock, raw, dest_addr, prob_error, prob_loss, delay_max_ms)


# ==========================
# Go-Back-N : ÉMETTEUR
# ==========================

def go_back_n_emitter(message_path: str,
                      prob_error: float = 0.0,
                      prob_loss: float = 0.0,
                      delay_max_ms: int = 0,
                      timeout_s: float = 0.3) -> None:
    """
    Émetteur Go-Back-N :

    - Découpe message_path en trames DATA.
    - Envoie avec fenêtre glissante.
    - Gère ACK et retransmissions sur timeout.
    - Termine avec une trame CON_CLOSE.
    """

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LOCALHOST, EMITTER_PORT))
    sock.settimeout(0.05)  # petits timeouts pour pouvoir vérifier les timers

    dest_addr = (LOCALHOST, RECEIVER_PORT)

    frames = split_file_into_frames(message_path)
    total_frames = len(frames)

    base = 0              # index de la première trame non acquittée
    next_to_send = 0      # index de la prochaine trame à envoyer
    send_time: dict[int, float] = {}

    nb_sent = 0
    nb_retrans = 0
    nb_acks = 0

    t0 = time.time()

    print(f"[EMETTEUR] Fichier '{message_path}' ({total_frames} trames).")
    print(f"[EMETTEUR] Paramètres canal: err={prob_error}, perte={prob_loss}, delaiMax={delay_max_ms} ms\n")

    while base < total_frames:
        # 1) Envoi des nouvelles trames dans la fenêtre
        while next_to_send < base + WINDOW_SIZE and next_to_send < total_frames:
            frame = frames[next_to_send]
            raw = encode_frame_to_bytes(frame)
            ok = send_through_channel(sock, raw, dest_addr,
                                      prob_error, prob_loss, delay_max_ms)
            if ok:
                print(f"[EMETTEUR] ENVOI DATA seq={frame.num_seq} (index={next_to_send})")
                send_time[next_to_send] = time.time()
                nb_sent += 1
            else:
                print(f"[EMETTEUR] PERTE (simulation) DATA seq={frame.num_seq}")
            next_to_send += 1

        # 2) Tentative de réception d'un ACK
        try:
            raw_ack, _ = sock.recvfrom(4096)
        except socket.timeout:
            raw_ack = None

        if raw_ack is not None:
            try:
                ack = decode_frame_from_bytes(raw_ack)
            except Exception as e:
                print(f"[EMETTEUR] ACK corrompu ou invalide : {e}")
            else:
                if ack.commande == CMD_ACK:
                    nb_acks += 1
                    ack_seq = ack.num_seq
                    print(f"[EMETTEUR] ACK reçu, last_good_seq={ack_seq}")
                    # Si ack_seq est dans l'intervalle [base .. total_frames-1],
                    # on avance le base à ack_seq + 1
                    if 0 <= ack_seq < total_frames and ack_seq >= base:
                        base = ack_seq + 1
                        # On peut aussi nettoyer les send_time obsolètes
                        for i in list(send_time.keys()):
                            if i < base:
                                del send_time[i]

        # 3) Gestion du timeout de la trame 'base'
        if base < next_to_send:
            first_index = base
            t_first = send_time.get(first_index)
            if t_first is not None and (time.time() - t_first) > timeout_s:
                print(f"[EMETTEUR] TIMEOUT sur index={first_index}, retransmission depuis index={first_index}")
                # Réémettre toutes les trames de [base .. next_to_send)
                for i in range(base, next_to_send):
                    frame = frames[i]
                    raw = encode_frame_to_bytes(frame)
                    ok = send_through_channel(sock, raw, dest_addr,
                                              prob_error, prob_loss, delay_max_ms)
                    if ok:
                        print(f"[EMETTEUR] RE-ENVOI DATA seq={frame.num_seq} (index={i})")
                        nb_sent += 1
                        nb_retrans += 1
                        send_time[i] = time.time()
                    else:
                        print(f"[EMETTEUR] PERTE (simulation) RE-ENVOI DATA seq={frame.num_seq}")

    # 4) Envoi d'une trame de fermeture CON_CLOSE
    close_frame = Frame(commande=CMD_CON_CLOSE, num_seq=total_frames & 0xFF, info=b"")
    raw_close = encode_frame_to_bytes(close_frame)
    send_through_channel(sock, raw_close, dest_addr,
                         prob_error, prob_loss, delay_max_ms)
    print(f"[EMETTEUR] ENVOI CON_CLOSE seq={close_frame.num_seq}")
    # On pourrait attendre un ACK final ici, mais pas strictement nécessaire pour les tests.

    elapsed = time.time() - t0
    print("\n[EMETTEUR] Terminé.")
    print(f"  Durée totale        : {elapsed:.3f} s")
    print(f"  Trames DATA envoyées: {nb_sent}")
    print(f"  Retransmissions     : {nb_retrans}")
    print(f"  ACK reçus           : {nb_acks}")

    sock.close()


# ==========================
# Go-Back-N : RECEPTEUR
# ==========================

def go_back_n_receiver(output_path: str,
                       prob_error: float = 0.0,
                       prob_loss: float = 0.0,
                       delay_max_ms: int = 0) -> None:
    """
    Récepteur Go-Back-N :

    - Reçoit des trames DATA dans l’ordre.
    - Vérifie CRC.
    - Rejette les trames hors ordre ou corrompues.
    - Concatène les payload dans output_path.
    - Envoie des ACK avec last_good_seq.
    """

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LOCALHOST, RECEIVER_PORT))
    # pas de timeout ici, on bloque jusqu'à réception

    print(f"[RECEPTEUR] Écriture dans '{output_path}'")
    print(f"[RECEPTEUR] Paramètres canal: err={prob_error}, perte={prob_loss}, delaiMax={delay_max_ms} ms\n")

    t0 = time.time()

    data_chunks: list[bytes] = []
    next_expected_seq = 0
    last_good_seq = -1
    nb_data_ok = 0
    nb_data_corrupted = 0
    nb_ack_sent = 0

    # On se souvient de l'adresse de l'émetteur dès la première trame
    emitter_addr = None

    while True:
        raw, addr = sock.recvfrom(4096)
        if emitter_addr is None:
            emitter_addr = addr

        # Tente de décoder la trame
        try:
            frame = decode_frame_from_bytes(raw)
        except Exception as e:
            print(f"[RECEPTEUR] Trame corrompue/destuff/CRC invalide: {e}")
            nb_data_corrupted += 1
            # On renvoie quand même un ACK du dernier bon
            send_ack(sock, emitter_addr, last_good_seq,
                     prob_error, prob_loss, delay_max_ms)
            nb_ack_sent += 1
            continue

        if frame.commande == CMD_CON_CLOSE:
            print(f"[RECEPTEUR] Reçu CON_CLOSE seq={frame.num_seq}, fermeture.")
            # On envoie un dernier ACK
            send_ack(sock, emitter_addr, last_good_seq,
                     prob_error, prob_loss, delay_max_ms)
            nb_ack_sent += 1
            break

        if frame.commande == CMD_DATA:
            print(f"[RECEPTEUR] Reçu DATA seq={frame.num_seq}, attendu={next_expected_seq}")
            if frame.num_seq == next_expected_seq:
                # Trame dans l'ordre, CRC déjà validé par decode_frame_from_bytes
                data_chunks.append(frame.info)
                last_good_seq = frame.num_seq
                next_expected_seq += 1
                nb_data_ok += 1
            else:
                # Trame hors ordre, on l'ignore (GBN)
                print(f"[RECEPTEUR] Trame hors ordre (reçue seq={frame.num_seq}, attendu={next_expected_seq}), ignorée.")

            # Envoie ACK du dernier bon
            send_ack(sock, emitter_addr, last_good_seq,
                     prob_error, prob_loss, delay_max_ms)
            nb_ack_sent += 1
        else:
            print(f"[RECEPTEUR] Trame avec commande inconnue/ignorée: {frame.commande}")

    # Écriture du fichier recomposé
    Path(output_path).write_bytes(b"".join(data_chunks))

    elapsed = time.time() - t0
    print("\n[RECEPTEUR] Terminé.")
    print(f"  Durée totale         : {elapsed:.3f} s")
    print(f"  Trames DATA correctes: {nb_data_ok}")
    print(f"  Trames corrompues    : {nb_data_corrupted}")
    print(f"  ACK envoyés          : {nb_ack_sent}")

    sock.close()


# ==========================
# MAIN (ligne de commande)
# ==========================

def main():
    """
    Usage:

      Récepteur :
        python protocole_py.py recv output.txt [err] [loss] [delay_ms]

      Émetteur :
        python protocole_py.py send message.txt [err] [loss] [delay_ms] [timeout_s]

    Exemples simples (canal parfait):

      # Terminal 1 (récepteur)
      python protocole_py.py recv output.txt

      # Terminal 2 (émetteur)
      python protocole_py.py send test.txt
    """
    if len(sys.argv) < 2:
        print(main.__doc__)
        return

    role = sys.argv[1]

    # Paramètres par défaut du canal
    prob_error = 0.0
    prob_loss  = 0.0
    delay_ms   = 0

    if role == "recv":
        if len(sys.argv) < 3:
            print("Usage: python protocole_py.py recv output.txt [err] [loss] [delay_ms]")
            return
        output_path = sys.argv[2]
        if len(sys.argv) > 3:
            prob_error = float(sys.argv[3])
        if len(sys.argv) > 4:
            prob_loss = float(sys.argv[4])
        if len(sys.argv) > 5:
            delay_ms = int(sys.argv[5])

        go_back_n_receiver(output_path, prob_error, prob_loss, delay_ms)

    elif role == "send":
        if len(sys.argv) < 3:
            print("Usage: python protocole_py.py send message.txt [err] [loss] [delay_ms] [timeout_s]")
            return
        message_path = sys.argv[2]
        timeout_s = 0.3
        if len(sys.argv) > 3:
            prob_error = float(sys.argv[3])
        if len(sys.argv) > 4:
            prob_loss = float(sys.argv[4])
        if len(sys.argv) > 5:
            delay_ms = int(sys.argv[5])
        if len(sys.argv) > 6:
            timeout_s = float(sys.argv[6])

        go_back_n_emitter(message_path, prob_error, prob_loss, delay_ms, timeout_s)

    else:
        print("Rôle inconnu. Utilise 'send' ou 'recv'.")
        print(main.__doc__)


if __name__ == "__main__":
    main()
