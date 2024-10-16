# Dascălu Ștefan-Nicolae  
## 321CA  

# TEMA 2 PROTOCOALE DE COMUNICATIE - APLICATIE CLIENT - SERVER

## 1. Server
  
Am folosit `hashmapuri` (`unordered_map`) pentru a păstra detalii despre clienți și topicurile la care sunt abonați:
- `online_clients` păstrează clienții care sunt online, în forma ID - file descriptor.
- `fds_to_id` este inversul lui `online_clients`, păstrează clienții în forma file descriptor - ID.
- `simple_topics_subscribes` - păstrează toate topicurile simple, fără wildcard, la care sunt abonați clienții.
- `wildcard_topics_subscribes` - păstrează toate topicurile cu wildcard la care sunt abonați clienții.
  
### Multiplexare:
Am folosit `poll` pentru a putea monitoriza în același timp mai mulți file descriptori. Există 4 situații:

1. **Primim date de la stdin**: 
   - Dacă comanda primită este `exit`, închidem conexiunea cu toți clienții TCP și programul se termină. Orice altă comanda este ignorată.

2. **Primim date de la un client UDP**: 
   - O să explic la final partea de încadrare a mesajelor. După ce s-a făcut mesajul pentru clienții TCP, se verifică prima dată dacă un client este abonat la topic în mod direct, fără a folosi wildcarduri. Dacă este, îi trimitem mesajul; altfel, se verifică și pentru topicurile cu wildcard. 
   - Pentru a verifica dacă topicul primit de la clientul UDP se potrivește cu cel la care este abonat clientul TCP, am folosit `strtok_r` ca să parcurg ambele topicuri în același timp. 
   - În cazul în care găsim un `+`, se trece la următorul token în ambele topicuri. Dacă sunt egale sau tokenul wildcard este `*` (pentru cazul în care avem topic de genul `upb/precis/+/*`), se trece mai departe; altfel, cele două nu se potrivesc. 
   - Dacă găsim `*`, se trece la următorul token în ambele topicuri, și după parcurgem topicul simplu până găsim un match sau se termină. Dacă se termină, nu s-au potrivit.

3. **Primim date pe socketul TCP**: 
   - Acceptăm noua conexiune și așteptăm următorul mesaj ca să primim ID-ul. Dacă nu există niciun user cu acel ID online, îl adăugăm în vectorul de `pollfds`. Vectorul este alocat dinamic, și se redimensionează atunci când se umple, deci clienții nu sunt limitați.

4. **Primim date de la un client TCP**: 
   - Dacă `recv` întoarce 0, clientul a închis conexiunea și îl ștergem din lista de file descriptori. Altfel, comanda e de tipul subscribe/unsubscribe. Verificăm dacă topicul conține wildcard, ca să știm cu ce hashmap vom lucra. 

## 2. Subscriber
  
Se conectează la server, și în următorul pachet trimite ID-ul.
Pentru client, avem doi file descriptors de unde putem primi date: stdin și socketul TCP pentru server. Am folosit `poll` și pentru client.
Dacă primim comanda `exit` de la stdin, oprim conexiunea cu serverul și programul se termină.
Ca să fie mai ușor de încadrat mesajele, am considerat că subscribe are tipul 5 și unsubscribe tipul 6.
Dacă primim date de la server, verificăm dacă `recv` returnează 0 (serverul a oprit conexiunea), altfel decodăm datele primite.

## 3. Încadrarea mesajelor

Pentru că protocolul TCP poate face concatenări/trunchieri și nu putem controla asta în mod direct, trebuie să avem un mecanism prin care să știm când am citit un pachet întreg. O soluție ușor de implementat ar fi să trimitem mereu un anumit număr de bytes, dar asta ar fi ineficient. O soluție bună este să punem la începutul unui pachet dimensiunea lui, ca să putem ști cât trebuie să citim.
Am folosit 2 tipuri de mesaje:
- **Server -> Subscriber**: `'length | IP_UDP | PORT_UDP | topic | type | payload'`
- **Subscriber -> Server**: `'length | type | topic'`

`Length` este dimensiunea totală a pachetului. Aceasta va varia în funcție de payload. Prin acest mecanism, o să știm exact câți bytes trebuie să primim / trimitem.
