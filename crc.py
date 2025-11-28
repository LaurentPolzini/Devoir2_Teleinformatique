import sys

flag = "01111110" # pour apres, flag a ajouter

def byte_to_bits(b: int) -> str:
    """Convertit un octet (0..255) en chaîne de 8 bits."""
    return format(b, "08b")

def bytes_to_bits(data: bytes) -> str:
    """Convertit une séquence d'octets en chaîne de bits."""
    return "".join(f"{b:08b}" for b in data)

def bits_to_bytes(bits: str) -> bytes:
    """
    Convertit une chaîne de bits ('0'/'1') en bytes.
    Si la longueur n'est pas de 8, erreur
    """
    #padding = (8 - (len(bits) % 8)) % 8
    #bits += "0" * padding
    if len(bits) % 8 != 0:
        print("")
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
    
    #print("message:",message)
    messageout =[]
    counter = 0
    


    for i in message:
        if i not in ("0", "1"):
            print("")
            raise ValueError("message doit contenir uniquement '0' et '1'")
        messageout.append(i)
        if i == "1":
            counter +=1
            if counter == 5:
                messageout.append("0")
                counter = 0
        if i == "0":
            counter = 0
    #print("message:",messageout)
    return "".join(messageout)




def destuff(message: str) -> str:
    
    #Retire les bits ajoutés par le bit-stuffing HDLC.
    
    #print("destuff in:",message)
    messageout = []
    count_ones = 0
    i = 0
    n = len(message)

    while i < n:
        b = message[i]
        if b not in ("0", "1"):
            print("")
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
                        f"(reçu '{stuffed_bit}' à index '{stuffed_index}')"
                    )

                #si pas d'erruer: on saute le bit stuffed
                i = stuffed_index # on avance jusqu'au stuffed
                count_ones = 0# on remet le compteur a zéro

        else:
            # reset compter a zero
            count_ones = 0

        i += 1
    #print("destuff out message",messageout)
    return "".join(messageout)


def add_flags(message: str) -> str:#add les flag une fois que stuffed.
    #print("avec flags",flag + message + flag)
    return flag + message + flag


def find_last_flag(message: str) -> int:
    return message.rfind(flag)
    

def remove_flags(bits: str) -> str:
    """Retire delimiter flags."""
    start = bits.find(flag)
    end = bits.rfind(flag)
    if start == -1 or end == -1 or end <= start:
        print("")
        raise ValueError("Flags not found in frame")
    return bits[start + len(flag):end]



def build_frame_with_crc16(command: int, numSeq: int, sizePayLoad: int, crc_builtin: int, payload: bytes) -> str:
    """
    Construit une trame HDLC tel que:
    FLAG | stuffing( bits(header + payload + CRC16) ) | FLAG

    Retourne une chaîne de bits avec flags ajoutés.
    """
    #core = en-tête + données (en bytes)
    core = bytes([command & 0xFF, numSeq & 0xFF, sizePayLoad & 0xFF]) + payload

    #CRC-16 sur core
    #crc_value = crc16_ccitt(core)
    #assert(crc_builtin == crc_value)

    #Convertir le CRC en 2 octets vu qu'on la comme entier et qu'on veut octets
    crc_bytes = crc_builtin.to_bytes(2, byteorder="big")

    #Ajouter le CRC a la fin du core
    core_with_crc = core + crc_bytes

    #Passer en bits
    core_bits = bytes_to_bits(core_with_crc)

    #Bit-stuffing
    stuffed_bits = stuffing(core_bits)

    #Ajout des flags (en bits)
    framed_bits = add_flags(stuffed_bits)

    return framed_bits


def parse_frame_with_crc16(frame_hex: str):
    """
    frame_hex: hex string received from C
    Returns: command, seq, size, crc, payload (bytes)
    """
    # Convert hex string to bytes
    framed_bytes = bytes.fromhex(frame_hex)
    
    framed_bits = bytes_to_bits(framed_bytes)
    
    core_bits = remove_flags(framed_bits)
    
    # Destuff
    core_bits = destuff(core_bits)
    
    # Bits to bytes
    core_bytes = bits_to_bytes(core_bits)
    
    if len(core_bytes) < 5:
        print("")
    
    # Split header + payload + CRC
    data_part = core_bytes[:-2]
    crc_bytes = core_bytes[-2:]
    crc = int.from_bytes(crc_bytes, byteorder="big")
    
    command = data_part[0]
    seq = data_part[1]
    size = data_part[2]
    payload_hex = (data_part[3:]).hex()
    
    return command, seq, size, crc, payload_hex



def crc_bits(bits: str) -> int:
    """
    Calcule le CRC-16 CCITT d'une chaîne de bits (0/1).
    On fait juste un padding en 0 pour arriver à un multiple de 8 bits.
    """
    # padding pour multiple de 8
    padding = (8 - (len(bits) % 8)) % 8
    bits_padded = bits + "0" * padding

    # conversion bits -> bytes (sans utiliser bits_to_bytes qui veut déjà un multiple de 8)
    out = bytearray()
    for i in range(0, len(bits_padded), 8):
        byte_str = bits_padded[i:i+8]
        out.append(int(byte_str, 2))

    return crc16_ccitt(bytes(out))


if __name__ == "__main__":
    args = sys.argv[1:]

    # Si aucun argument => mode test bit-stuffing + CRC
    if not args:
        demande = 3
    else:
        demande = int(args[0])

    if demande == 0:
        comm = int(args[1])
        numSeq = int(args[2])
        siz = int(args[3])
        crc = int(args[4])
        if siz > 0:
            framed_bits = build_frame_with_crc16(comm, numSeq, siz, crc, bytes.fromhex(args[5]))
        else:
            framed_bits = ""
        print(framed_bits)

    elif demande == 1:
        coreDatas = bytes.fromhex(args[1])
        print(crc16_ccitt(coreDatas))

    elif demande == 2:
        command, seq, size, crc, payload = parse_frame_with_crc16(args[1])
        print(f"{command}:{seq}:{size}:{crc}:{payload}")

    elif demande == 3:
        # === TEST COMPLET BIT-STUFFING + CRC + CORRUPTION ===
        original_bits = "011111101111101111110111110"
        print("Flux original :", original_bits)

        stuffed = stuffing(original_bits)
        print("Après stuffing :", stuffed)

        destuffed = destuff(stuffed)
        print("Après destuff  :", destuffed)

        crc_orig = crc_bits(original_bits)
        crc_dest = crc_bits(destuffed)
        print("CRC(original)  :", crc_orig)
        print("CRC(destuffed) :", crc_dest)

        # --- Test de corruption d'un bit stuffed ---
        # On cherche un motif 0 + 5 x '1' + 0 => le 0 final est le bit stuffed
        motif = "0111110"
        idx = stuffed.find(motif)
        if idx != -1:
            stuffed_list = list(stuffed)
            stuffed_zero_index = idx + len(motif) - 1  # index du bit stuffed '0'
            # on corrompt ce bit stuffed (0 -> 1)
            stuffed_list[stuffed_zero_index] = "1"
            corrupted = "".join(stuffed_list)
            print("Trame corrompue :", corrupted)

            try:
                destuff(corrupted)
                print("ERREUR : la corruption n'a PAS été détectée !")
            except ValueError as e:
                print("Corruption détectée par destuff :", e)
        else:
            print("Motif de bit stuffed non trouvé dans la trame (improbable avec cet exemple).")


    
