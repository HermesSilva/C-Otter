-- Fixtures do MySQL para testar o catalogo contra um servidor real.
--
-- Existe pelo mesmo motivo do fixtures.sql do PostgreSQL: implementar o no'
-- de "Triggers" sem ter uma trigger no banco significa entregar codigo que
-- nunca rodou (diretiva 2).
--
--   mysql -h localhost -u root -p < tests/integration/fixtures_mysql.sql

DROP DATABASE IF EXISTS otter_test;
CREATE DATABASE otter_test DEFAULT CHARACTER SET utf8mb4;
USE otter_test;

-- Tabela base, com auto_increment, indice composto e comentarios.
CREATE TABLE cliente (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    nome        VARCHAR(120) NOT NULL COMMENT 'Razao social',
    documento   CHAR(14) NOT NULL,
    situacao    ENUM('ativo', 'inativo', 'suspenso') NOT NULL DEFAULT 'ativo',
    limite      DECIMAL(12,2) DEFAULT 0.00,
    observacao  TEXT,
    anexo       BLOB,
    criado_em   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uq_cliente_documento (documento),
    KEY ix_cliente_nome_situacao (nome, situacao)
) ENGINE=InnoDB COMMENT='Clientes do sistema';

-- Tabela dependente: exercita a FK nas duas direcoes (chaves desta tabela e
-- referencias a cliente).
CREATE TABLE pedido (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    cliente_id  INT NOT NULL,
    total       DECIMAL(12,2) NOT NULL,
    emitido_em  DATETIME NOT NULL,
    CONSTRAINT fk_pedido_cliente FOREIGN KEY (cliente_id)
        REFERENCES cliente (id) ON DELETE CASCADE ON UPDATE RESTRICT
) ENGINE=InnoDB;

-- Chave primaria COMPOSTA: a ordem das colunas precisa sobreviver.
CREATE TABLE pedido_item (
    pedido_id   INT NOT NULL,
    sequencia   SMALLINT NOT NULL,
    descricao   VARCHAR(200) NOT NULL,
    quantidade  INT NOT NULL DEFAULT 1,
    PRIMARY KEY (pedido_id, sequencia),
    CONSTRAINT fk_item_pedido FOREIGN KEY (pedido_id)
        REFERENCES pedido (id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE VIEW cliente_ativo AS
    SELECT id, nome, documento, limite
      FROM cliente
     WHERE situacao = 'ativo';

CREATE TRIGGER trg_pedido_antes_inserir
    BEFORE INSERT ON pedido
    FOR EACH ROW
    SET NEW.emitido_em = IFNULL(NEW.emitido_em, NOW());

DELIMITER //

CREATE PROCEDURE sp_total_do_cliente(IN p_cliente INT, OUT p_total DECIMAL(12,2))
    COMMENT 'Soma os pedidos de um cliente'
BEGIN
    SELECT COALESCE(SUM(total), 0) INTO p_total
      FROM pedido WHERE cliente_id = p_cliente;
END //

CREATE FUNCTION fn_limite_disponivel(p_cliente INT) RETURNS DECIMAL(12,2)
    DETERMINISTIC
    READS SQL DATA
BEGIN
    DECLARE v_limite DECIMAL(12,2);
    DECLARE v_usado  DECIMAL(12,2);

    SELECT limite INTO v_limite FROM cliente WHERE id = p_cliente;
    SELECT COALESCE(SUM(total), 0) INTO v_usado FROM pedido WHERE cliente_id = p_cliente;

    RETURN v_limite - v_usado;
END //

DELIMITER ;

INSERT INTO cliente (nome, documento, situacao, limite, observacao) VALUES
    ('Alfa Comercio Ltda',   '11111111000101', 'ativo',    10000.00, 'cliente antigo'),
    ('Beta Servicos ME',     '22222222000102', 'inativo',   5000.00, NULL),
    ('Gama Industria S.A.',  '33333333000103', 'ativo',    75000.00, 'acento: coração');

INSERT INTO pedido (cliente_id, total, emitido_em) VALUES
    (1, 1500.00, '2026-01-10 09:00:00'),
    (1, 2300.50, '2026-02-14 15:30:00'),
    (3,  980.25, '2026-03-01 11:45:00');

INSERT INTO pedido_item (pedido_id, sequencia, descricao, quantidade) VALUES
    (1, 1, 'Item A', 2),
    (1, 2, 'Item B', 1),
    (2, 1, 'Item C', 5);

-- Tabela SEM chave primaria: a grade precisa recusar a edicao com uma
-- explicacao, e nao fingir que salvou.
CREATE TABLE registro_sem_pk (
    origem      VARCHAR(50),
    valor       INT
) ENGINE=InnoDB;

INSERT INTO registro_sem_pk VALUES ('a', 1), ('b', 2);
