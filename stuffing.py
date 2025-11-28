#Code pour bit stuffing 
flag = "01111110" # pour apres, flag a ajouter




def stuffing(message: str) -> str:
    #message est u
    
    print("message in :",message)
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
    print("message out:","".join(messageout))
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

        messageout.append(b)

        if b == "1":
            count_ones += 1
            if count_ones == 5:
                # si bit suivant est '0' onsaute (stuffed)
                if i + 1 < n and message[i + 1] == "0":
                    i += 1  # on saute le 0
                count_ones = 0
        else:
            count_ones = 0

        i += 1
    print("destuff out message","".join(messageout))
    return "".join(messageout)


def add_flags(message: str) -> str:#add les flag une fois que stuffed.
    print("avec flags",flag + message + flag)
    return flag + message + flag


def remove_flags(message: str) -> str:
    
    if not message.startswith(flag) or not message.endswith(flag):
        raise ValueError("flags pas présents, erreur")
    print("flags enlevés:",message[len(flag):-len(flag)])
    return message[len(flag):-len(flag)]

test = destuff(stuffing("011111101111101111110111110"))

test2 =remove_flags(add_flags(test))





