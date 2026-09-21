-- Objetos de teste para conferir a arvore do Navigator.
--
-- Por que existe: o ERP_TID tem 32 tabelas e nada mais -- nenhuma view,
-- nenhum tipo proprio, nenhuma particao. Implementar o no de "Views" sem ter
-- uma view no banco significaria entregar codigo que nunca rodou.
--
-- Todos os objetos ficam no schema `otter_test`, isolado do ERP_TID: rodar e
-- desfazer este script nao toca em nada do sistema real.
--
--   psql -h localhost -U postgres -d ERP_TID -f tests/integration/fixtures.sql

DROP SCHEMA IF EXISTS otter_test CASCADE;
CREATE SCHEMA otter_test;
SET search_path TO otter_test;

-- --- Tabelas base ----------------------------------------------------------

CREATE TABLE cliente (
    cliente_id   serial PRIMARY KEY,
    nome         varchar(120) NOT NULL,
    email        varchar(200) UNIQUE,
    credito      numeric(12,2) NOT NULL DEFAULT 0 CHECK (credito >= 0),
    criado_em    timestamptz NOT NULL DEFAULT now()
);
COMMENT ON TABLE cliente IS 'Cadastro de clientes -- fixture do C-Otter';
COMMENT ON COLUMN cliente.credito IS 'Limite aprovado, nunca negativo';

CREATE TABLE pedido (
    pedido_id    serial PRIMARY KEY,
    cliente_id   integer NOT NULL REFERENCES cliente(cliente_id)
                     ON UPDATE CASCADE ON DELETE RESTRICT,
    total        numeric(12,2) NOT NULL DEFAULT 0,
    situacao     varchar(20) NOT NULL DEFAULT 'aberto',
    emitido_em   date NOT NULL DEFAULT current_date
);

CREATE INDEX ix_pedido_cliente  ON pedido (cliente_id);
CREATE INDEX ix_pedido_situacao ON pedido (situacao) WHERE situacao <> 'cancelado';

-- --- Views -----------------------------------------------------------------
--
-- Uma view nao tem constraints nem chaves estrangeiras, mas PODE ter trigger
-- (INSTEAD OF) e regras. A arvore precisa refletir isso.

CREATE VIEW vw_cliente_ativo AS
SELECT cliente_id, nome, email, credito
  FROM cliente
 WHERE credito > 0;
COMMENT ON VIEW vw_cliente_ativo IS 'Clientes com credito aprovado';

CREATE VIEW vw_pedido_resumo AS
SELECT c.nome,
       count(p.pedido_id) AS pedidos,
       COALESCE(sum(p.total), 0) AS faturado
  FROM cliente c
  LEFT JOIN pedido p ON p.cliente_id = c.cliente_id
 GROUP BY c.nome;

-- --- Materialized view -----------------------------------------------------
--
-- Tem indices (ao contrario da view comum) e NAO tem triggers.

CREATE MATERIALIZED VIEW mvw_faturamento_mes AS
SELECT date_trunc('month', emitido_em)::date AS mes,
       count(*) AS pedidos,
       sum(total) AS faturado
  FROM pedido
 GROUP BY 1;

CREATE UNIQUE INDEX ux_mvw_faturamento_mes ON mvw_faturamento_mes (mes);
COMMENT ON MATERIALIZED VIEW mvw_faturamento_mes IS 'Faturamento consolidado por mes';

-- --- Trigger ---------------------------------------------------------------

CREATE FUNCTION fn_pedido_auditoria() RETURNS trigger
LANGUAGE plpgsql AS $$
BEGIN
    -- Sem efeito real: a fixture existe para a arvore ter um trigger para
    -- mostrar, nao para testar a logica.
    RETURN NEW;
END;
$$;

CREATE TRIGGER tg_pedido_auditoria
    AFTER INSERT OR UPDATE ON pedido
    FOR EACH ROW EXECUTE FUNCTION fn_pedido_auditoria();

-- --- Tipos proprios --------------------------------------------------------

CREATE TYPE situacao_pedido AS ENUM ('aberto', 'faturado', 'cancelado');
CREATE DOMAIN cnpj AS varchar(14) CHECK (VALUE ~ '^[0-9]{14}$');
CREATE TYPE endereco AS (logradouro varchar(200), numero varchar(10), cep varchar(8));

-- --- Rotinas ---------------------------------------------------------------

CREATE FUNCTION fn_credito_disponivel(p_cliente integer)
RETURNS numeric LANGUAGE sql STABLE AS $$
    SELECT c.credito - COALESCE(
               (SELECT sum(p.total) FROM pedido p
                 WHERE p.cliente_id = c.cliente_id
                   AND p.situacao = 'aberto'), 0)
      FROM cliente c WHERE c.cliente_id = p_cliente;
$$;

CREATE PROCEDURE sp_cancelar_pedido(p_pedido integer)
LANGUAGE plpgsql AS $$
BEGIN
    UPDATE pedido SET situacao = 'cancelado' WHERE pedido_id = p_pedido;
END;
$$;

-- --- Dados -----------------------------------------------------------------

INSERT INTO cliente (nome, email, credito) VALUES
    ('Lontra Marinha Ltda', 'contato@lontra.example',  15000.00),
    ('Rio Corrente S/A',    'fin@riocorrente.example',  8200.50),
    ('Pedra do Bolso ME',   NULL,                          0.00);

INSERT INTO pedido (cliente_id, total, situacao, emitido_em) VALUES
    (1, 2500.00, 'faturado',  current_date - 40),
    (1, 1200.00, 'aberto',    current_date - 5),
    (2,  780.25, 'faturado',  current_date - 12),
    (2, 3100.00, 'cancelado', current_date - 2);

REFRESH MATERIALIZED VIEW mvw_faturamento_mes;

-- Confere o que foi criado.
SELECT c.relkind, count(*)
  FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
 WHERE n.nspname = 'otter_test'
 GROUP BY 1 ORDER BY 1;
