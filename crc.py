flag = "01111110" # pour apres, flag a ajouter

def byte_to_bits(b: int) -> str:
    """Convertit un octet (0..255) en chaîne de 8 bits."""
    return format(b, "08b")

def bytes_to_bits(data: bytes) -> str:
    """Convertit une séquence d'octets en chaîne de bits."""
    return "".join(byte_to_bits(b) for b in data)


def bits_to_bytes(bits: str) -> bytes:
    """
    Convertit une chaîne de bits ('0'/'1') en bytes.
    Longueur de bits doit être multiple de 8.
    """
    if len(bits) % 8 != 0:
        raise ValueError("Longueur de la chaîne de bits non multiple de 8")

    out = bytearray()
    for i in range(0, len(bits), 8):
        byte_str = bits[i:i+8]
        out.append(int(byte_str, 2))
    return bytes(out)


def crc16_ccitt(data: bytes, poly: int = 0x1021, init_value: int = 0xFFFF) -> int:
    """
    Calcule le CRC-16 (CCITT) sur 'data'.
    data : seq d'octets (bytes ou bytearray)
    poly : poly gen. (défaut 0x1021)
    init_value : valeur initiale du registre CRC (0xFFFF), certaines versions utilise d'autre chose
    Retourne un entier sur 16 bits (0..65535).
    """
    crc = init_value  # registre 16 bits, changeable selon versions

    for byte in data:
        # On place l'octet qu'on traite dans les 8 bits de poids fort du CRC
        crc ^= (byte << 8)  # equivalent d'ajouter 8 zéros en binaire 

        # On traite l'octet bit par bit
        for _ in range(8):#8 bits par octet
            # Si le bit de poids fort est à 1
            if crc & 0x8000:
                # On décale à gauche et on applique le polynôme
                crc = ((crc << 1) & 0xFFFF) ^ poly
            else:
                # Sinon on décale juste à gauche
                crc = (crc << 1) & 0xFFFF

    return crc
    #note pour compréhension, c'est comme si on passait a travers 8 par 8, mais en soit c'est aussi possible
    #théoriquement de tout "grouper" en un bloc de 128 bits (8 octets) et le traiter comme ca
    





def stuffing(message: str) -> str:
    #message est u
    
    print("message:",message)
    messageout =[]
    counter = 0
    


    for i in message:
        if i not in ("0", "1"):
            raise ValueError("message doit contenir uniquement '0' et '1'")
        messageout.append(i)
        if i == "1":
            counter +=1
            if counter == 5:
                messageout.append("0")
                counter = 0
        if i == "0":
            counter = 0
    print("message:",messageout)
    return "".join(messageout)




def destuff(message: str) -> str:
    
    #Retire les bits ajoutés par le bit-stuffing HDLC.
    
    print("destuff in:",message)
    messageout = []
    count_ones = 0
    i = 0
    n = len(message)

    while i < n:
        b = message[i]
        if b not in ("0", "1"):
            raise ValueError("message doit contenir uniquement '0' et '1'")

        #add bit courant a la sortie
        messageout.append(b)

        if b == "1":
            count_ones += 1

            if count_ones == 5:
                stuffed_index = i + 1

                #Si pas de bit après les 5 '1' alors erreur
                if stuffed_index >= n:
                    raise ValueError(
                        "Trame corrompue : bit stuffed manquant après 5 bits à '1'"
                    )

                stuffed_bit = message[stuffed_index]#celui d'apres en gros vu que stuffed index est i+1

                #stuffed bit doit etre 0 sinon corruption
                if stuffed_bit != "0":
                    raise ValueError(
                        "Trame corrompue : bit stuffed différent de '0' "
                        f"(reçu '{stuffed_bit}' à '{stuffed_index}')"
                    )

                #si pas d'erruer: on saute le bit stuffed
                i = stuffed_index # on avance jusqu'au stuffed
                count_ones = 0# on remet le compteur a zéro

        else:
            # reset compter a zero
            count_ones = 0

        i += 1
    print("destuff out message",messageout)
    return "".join(messageout)


def add_flags(message: str) -> str:#add les flag une fois que stuffed.
    print("avec flags",flag + message + flag)
    return flag + message + flag


def remove_flags(message: str) -> str:
    
    if not message.startswith(flag) or not message.endswith(flag):
        raise ValueError("flags pas présents, erreur")
    print("flags enlevés:",message[len(flag):-len(flag)])
    return message[len(flag):-len(flag)]

def build_frame_with_crc16(address: int, command: int, payload: bytes) -> str:
    """
    Construit une trame HDLC tel que:
    FLAG | stuffing( bits(header + payload + CRC16) ) | FLAG

    Retourne une chaîne de bits avec flags ajoutés.
    """
    #core = en-tête + données (en bytes)
    core = bytes([address & 0xFF, command & 0xFF]) + payload

    #CRC-16 sur core
    crc_value = crc16_ccitt(core)

    #Convertir le CRC en 2 octets vu qu'on la comme entier et qu'on veut octets
    crc_bytes = crc_value.to_bytes(2, byteorder="big")

    #Ajouter le CRC a la fin du core
    core_with_crc = core + crc_bytes

    #Passer en bits
    core_bits = bytes_to_bits(core_with_crc)

    #Bit-stuffing
    stuffed_bits = stuffing(core_bits)

    #Ajout des flags (en bits)
    framed_bits = add_flags(stuffed_bits)

    return framed_bits


def parse_frame_with_crc16(framed_bits: str):
    """
    Prend une trame en bits (avec flags, stuffed),
    vérifie le CRC, et retourne (address, command, payload) si OK.

    Lève ValueError si CRC invalide ou trame trop courte.
    """
    #Enlever les flags
    without_flags = remove_flags(framed_bits)  # bits stuffed, sans flags

    #Destuffing
    core_bits = destuff(without_flags)         # bits originaux (header+payload+CRC)

    #Bits -> bytes
    core_bytes = bits_to_bytes(core_bits)

    if len(core_bytes) < 4:
        # il faut au moins: 1 octet adresse, 1 octet commande, 2 octets CRC
        raise ValueError("Trame trop courte pour contenir header + CRC")

    #separer la partie data et le CRC recu
    data_part = core_bytes[:-2]# header + payload
    received_crc_bytes = core_bytes[-2:]# 2 octets de CRC recus
    received_crc = int.from_bytes(received_crc_bytes, byteorder="big")

    #Recalcul CRC pr verif
    computed_crc = crc16_ccitt(data_part)

    if computed_crc != received_crc:
        raise ValueError(
            f"CRC invalide: reçu=0x{received_crc:04X}, calculé=0x{computed_crc:04X}"
        )

    #Extraire adresse, commande, payload
    address = data_part[0]
    command = data_part[1]
    payload = data_part[2:]#bytes

    return address, command, payload





if __name__ == "__main__":
    # test
    addr = 0x01
    cmd  = 0x02
    payload = b"TEST"

    print("=== ÉMISSION ===")
    frame_bits = build_frame_with_crc16(addr, cmd, payload)
    print("Trame émise (bits):", frame_bits)

    print("\n=== RÉCEPTION ===")
    #simulation reception
    a, c, p = parse_frame_with_crc16(frame_bits)
    print("Adresse reçue:", a)
    print("Commande reçue:", c)
    print("Payload reçu:", p)