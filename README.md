# SO 24/25 - Projeto 1
**Filipe Oliveira**  
**Número de estudante**: ist1110633

## 📌 Descrição do Projeto  

O objetivo deste projeto é desenvolver o **IST Key Value Store (IST-KVS)**, um sistema de **armazenamento de dados** baseado em pares **chave-valor**.  

Os pares **chave-valor** são armazenados numa **tabela de dispersão (hashtable)**, onde:  
- **A chave** define a posição na tabela.  
- **O valor** é um vetor de bytes associado a essa chave.  

Além disso, o projeto envolve **paralelização e sincronização** de modo a acelerar o processamento, além do suporte a **backups assíncronos** usando processos.  

---

## 🎯 Funcionalidade  

O **IST-KVS** aceita **vários comandos** para manipulação dos dados, permitindo:  
- **Armazenar, atualizar, remover e ler pares chave-valor**.  
- **Lidar com colisões na hashtable**.  
- **Executar backups não bloqueantes**.  
- **Processar múltiplos ficheiros de comandos em paralelo**.  

### 📌 **Comandos Disponíveis**  

| Comando | Descrição |
|---------|-----------|
| `WRITE [(c1,v1) (c2,v2)]` | Escreve ou atualiza pares chave-valor. |
| `READ [c1, c2]` | Lê os valores associados às chaves fornecidas. |
| `DELETE [c1, c2]` | Remove os pares chave-valor indicados. |
| `SHOW` | Exibe todos os pares armazenados. |
| `WAIT <ms>` | Introduz um atraso na execução (útil para testes). |
| `BACKUP` | Realiza uma cópia de segurança do estado atual. |
| `HELP` | Mostra informações sobre os comandos disponíveis. |

---

## 🏗️ Estrutura do Projeto  

A implementação foi feita em **C**, utilizando:  
- **Sistema de ficheiros POSIX** para leitura/escrita.  
- **Processos (fork)** para backups assíncronos.  
- **Threads** para processamento paralelo de ficheiros `.job`.  

O projeto está dividido em **três exercícios**:  

### 📝 **Exercício 1: Processamento de ficheiros de comandos**  
- O IST-KVS deixa de aceitar comandos apenas via terminal e passa a processar **ficheiros `.job`**.  
- Para cada ficheiro `.job` processado, será gerado um ficheiro de saída `.out`.  

### 💾 **Exercício 2: Backups Não Bloqueantes**  
- Sempre que o comando `BACKUP` for executado, um **processo filho** será criado para gravar um ficheiro `<nome>.bck`.  
- Se um **limite de backups concorrentes** for atingido, novos backups aguardam até haver espaço.  

### 🚀 **Exercício 3: Paralelização com Threads**  
- Diferentes ficheiros `.job` serão processados **em paralelo** utilizando **threads**.  
- O número máximo de threads será definido na linha de comando.  
- Garantia de **execução atómica** para evitar problemas de concorrência.  

---

📄 **Nota:** Para mais informações e explicações detalhadas, consultar [enunciado-SO2425P1.pdf](./enunciado-SO2425P1.pdf)